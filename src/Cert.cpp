// Cert.cpp (refactored: per-instance caches, no locks)

#include "Cert.hpp"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include <chrono>
#include <filesystem>
#include <cstdlib>

#include <unistd.h>  // mkstemps, unlink
#include <curl/curl.h>

#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/buffer.h>  // BUF_MEM for BIO_get_mem_ptr
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

// ---------- file-scope helpers ----------
namespace {
constexpr int kAiaTtlSeconds = 24 * 60 * 60;  // 24h for positive cache
constexpr int kAiaNegTtlSeconds = 10 * 60;    // 10m for negative cache
}  // namespace

// Extract host (no scheme, no port) from a URL string.
static std::string HostFromUrl(const std::string& url) {
  const auto scheme_pos = url.find("://");
  const size_t host_begin =
    (scheme_pos == std::string::npos) ? 0 : scheme_pos + 3;
  const auto path_pos = url.find('/', host_begin);
  std::string hostport = url.substr(host_begin, path_pos - host_begin);
  const auto colon = hostport.find(':');
  if (colon != std::string::npos)
    hostport.erase(colon);  // strip :port
  return hostport;
}

// keep letters, digits, '.', '-', '_' ; replace others with '_'
static std::string SanitizeForFilename(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  auto ok = [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '-' || c == '_';
  };
  for (unsigned char c : s)
    out.push_back(ok(c) ? char(c) : '_');
  return out;
}

static size_t WriteStrCb(char* p, size_t sz, size_t nm, void* ud) {
  auto* s = static_cast<std::string*>(ud);
  s->append(p, sz * nm);
  return sz * nm;
}

// ---------- TempPem ----------

TempPem::~TempPem() {
  if (!path.empty())
    ::unlink(path.c_str());
}

// ---------- Cert helpers (static) ----------

bool Cert::HttpGetToString(const std::string& url, std::string& out) {
  if (Cert::TestHooks::fake_http_response) {
    out = *Cert::TestHooks::fake_http_response;
    return true;
  }
  CURL* h = curl_easy_init();
  if (!h)
    return false;

  curl_easy_setopt(h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(h, CURLOPT_USERAGENT, "curl/7.x (crawler)");
  curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, &WriteStrCb);
  curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
  curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 10000L);

  struct curl_slist* hdrs = nullptr;
  hdrs =
    curl_slist_append(hdrs,
                      "Accept: application/pkix-cert, application/pkcs7-mime, "
                      "application/x-pkcs7-certificates, "
                      "application/x-x509-ca-cert;q=0.9, */*;q=0.5");
  curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);

  const auto rc = curl_easy_perform(h);
  if (hdrs)
    curl_slist_free_all(hdrs);
  curl_easy_cleanup(h);
  return rc == CURLE_OK;
}

std::string Cert::EnsurePem(const std::string& der_or_pem) {
  if (der_or_pem.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
    return der_or_pem;  // already PEM
  }
  // Try DER → single X509
  const unsigned char* p =
    reinterpret_cast<const unsigned char*>(der_or_pem.data());
  X509* x = d2i_X509(nullptr, &p, static_cast<long>(der_or_pem.size()));
  if (x) {
    std::string pem;
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio) {
      if (PEM_write_bio_X509(bio, x)) {
        BUF_MEM* mem = nullptr;
        BIO_get_mem_ptr(bio, &mem);
        if (mem && mem->data && mem->length > 0) {
          pem.assign(mem->data, mem->length);
        }
      }
      BIO_free(bio);
    }
    X509_free(x);
    if (!pem.empty())
      return pem;
  }

  // Try CMS/PKCS#7 “certs only” (common AIA response, .p7c)
  std::string pem_multi;
  BIO* in = BIO_new_mem_buf(der_or_pem.data(), (int)der_or_pem.size());
  if (in) {
    CMS_ContentInfo* ci = d2i_CMS_bio(in, nullptr);
    if (ci) {
      STACK_OF(X509)* certs = CMS_get1_certs(ci);  // caller owns returned stack
      if (certs) {
        BIO* out = BIO_new(BIO_s_mem());
        if (out) {
          for (int i = 0; i < sk_X509_num(certs); ++i) {
            X509* c = sk_X509_value(certs, i);
            // Append each as PEM
            if (PEM_write_bio_X509(out, c) != 1)
              continue;
          }
          BUF_MEM* mem = nullptr;
          BIO_get_mem_ptr(out, &mem);
          if (mem && mem->data && mem->length > 0) {
            pem_multi.assign(mem->data, mem->length);
          }
          BIO_free(out);
        }
        sk_X509_pop_free(certs, X509_free);
      }
      CMS_ContentInfo_free(ci);
    }
    BIO_free(in);
  }
  if (!pem_multi.empty())
    return pem_multi;

  return {};
}

std::string Cert::ExtractIssuerCNFromPem(const std::string& pem) {
  std::string cn;
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (!bio)
    return cn;

  X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!x)
    return cn;

  X509_NAME* iss = X509_get_issuer_name(x);
  if (iss) {
    char buf[512] = {0};
    int n =
      X509_NAME_get_text_by_NID(iss, NID_commonName, buf, sizeof(buf) - 1);
    if (n > 0)
      cn.assign(buf, static_cast<size_t>(n));
  }
  X509_free(x);
  return cn;
}

std::string Cert::FingerprintSha1Hex(const std::string& pem) {
  std::string hex;
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (!bio)
    return hex;

  X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!x)
    return hex;

  unsigned int n = 0;
  unsigned char md[EVP_MAX_MD_SIZE];
  if (X509_digest(x, EVP_sha1(), md, &n)) {
    static const char* d = "0123456789abcdef";
    hex.resize(n * 2);
    for (unsigned i = 0; i < n; ++i) {
      hex[2 * i] = d[(md[i] >> 4) & 0xF];
      hex[2 * i + 1] = d[md[i] & 0xF];
    }
  }
  X509_free(x);
  return hex;
}

// Optional: SHA-256 fingerprint (used for cache key in some flows)
std::string Cert::LeafFingerprintSha256Hex(const std::string& pem) {
  std::string hex;
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (!bio)
    return hex;

  X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!x)
    return hex;

  unsigned int n = 0;
  unsigned char md[EVP_MAX_MD_SIZE];
  if (X509_digest(x, EVP_sha256(), md, &n)) {
    static const char* d = "0123456789abcdef";
    hex.resize(n * 2);
    for (unsigned i = 0; i < n; ++i) {
      hex[2 * i] = d[(md[i] >> 4) & 0xF];
      hex[2 * i + 1] = d[md[i] & 0xF];
    }
  }
  X509_free(x);
  return hex;
}

// Parse AIA "CA Issuers" URLs from PEM (PEM -> X509 -> AUTHORITY_INFO_ACCESS)
std::vector<std::string> Cert::AiaCaIssuersFromPem(const std::string& pem) {
  std::vector<std::string> out;

  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (!bio)
    return out;

  X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!x)
    return out;

  auto* aia = static_cast<STACK_OF(ACCESS_DESCRIPTION)*>(
    X509_get_ext_d2i(x, NID_info_access, nullptr, nullptr));

  if (aia) {
    const int n = sk_ACCESS_DESCRIPTION_num(aia);
    for (int i = 0; i < n; ++i) {
      const ACCESS_DESCRIPTION* ad = sk_ACCESS_DESCRIPTION_value(aia, i);
      if (!ad || OBJ_obj2nid(ad->method) != NID_ad_ca_issuers)
        continue;
      if (ad->location->type == GEN_URI && ad->location->d.ia5) {
        const unsigned char* d = ad->location->d.ia5->data;
        const int len = ad->location->d.ia5->length;
        if (d && len > 0)
          out.emplace_back(reinterpret_cast<const char*>(d),
                           static_cast<size_t>(len));
      }
    }
    AUTHORITY_INFO_ACCESS_free(aia);
  }
  X509_free(x);
  return out;
}

// Extract the leaf certificate PEM from CURLINFO_CERTINFO
std::string Cert::LeafPemFromCertinfo(CURL* easy) {
  curl_certinfo* ci = nullptr;
  if (curl_easy_getinfo(easy, CURLINFO_CERTINFO, &ci) != CURLE_OK || !ci) {
    return {};
  }
  if (ci->num_of_certs <= 0 || !ci->certinfo || !ci->certinfo[0])
    return {};

  // The leaf is index 0
  const curl_slist* p = ci->certinfo[0];
  std::string pem;
  for (; p; p = p->next) {
    const char* line = p->data ? p->data : "";
    // Lines look like "Cert:" <PEM...> or "Subject:", "Issuer:", etc
    static const char kKey[] = "Cert:";
    if (std::strncmp(line, kKey, sizeof(kKey) - 1) == 0) {
      const char* val = line + (sizeof(kKey) - 1);
      while (*val == ' ' || *val == '\t')
        ++val;
      pem.assign(val);
      break;
    }
  }
  // Sometimes libcurl provides DER in the "Cert:" value; normalize.
  return EnsurePem(pem);
}

// Discover AIA URLs for a URL's leaf cert, with per-instance caches.
std::vector<std::string> Cert::ExtractAiaUrls(const std::string& url) {
  using clock = std::chrono::steady_clock;
  const auto now = clock::now();
  const std::string host = HostFromUrl(url);

  // Fast path: host-level cache (no lock; per-instance)
  if (auto it = aia_by_host_.find(host);
      it != aia_by_host_.end() && it->second.expires > now) {
    return it->second.urls;  // may be empty (negative)
  }

  std::vector<std::string> urls;

  CURL* h = curl_easy_init();
  if (!h)
    return urls;

  curl_easy_setopt(h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(h, CURLOPT_CERTINFO, 1L);
  curl_easy_setopt(h, CURLOPT_USERAGENT, "curl/7.x (crawler)");

  // We just want the leaf's AIA; don't block on verification here.
  curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);

  // Be a good citizen on flaky hosts
  curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
  curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 8000L);

  CURLcode code;
  if (Cert::TestHooks::force_perform_result) {
    code = *Cert::TestHooks::force_perform_result;
  } else {
    code = curl_easy_perform(h);
  }

  if (code == CURLE_OK) {
    std::string pem = LeafPemFromCertinfo(h);
    if (!pem.empty()) {
      const std::string fp = LeafFingerprintSha256Hex(pem);

      // Fingerprint cache: does this leaf map already exist?
      if (!fp.empty()) {
        if (auto it = aia_by_fp_.find(fp);
            it != aia_by_fp_.end() && it->second.expires > now) {
          aia_by_host_[host] = it->second;  // mirror
          curl_easy_cleanup(h);
          return it->second.urls;
        }
      }

      // Parse AIA from PEM
      urls = AiaCaIssuersFromPem(pem);

      // Insert into caches (positive / negative)
      AiaCacheEntry entry;
      entry.urls = urls;  // may be empty
      entry.negative = urls.empty();
      entry.expires =
        now + std::chrono::seconds(entry.negative ? kAiaNegTtlSeconds
                                                  : kAiaTtlSeconds);

      if (!fp.empty())
        aia_by_fp_[fp] = entry;
      aia_by_host_[host] = entry;

      // crude size caps
      if (aia_by_fp_.size() > 4096)
        aia_by_fp_.clear();
      if (aia_by_host_.size() > 4096)
        aia_by_host_.clear();
    }
  }

  curl_easy_cleanup(h);
  return urls;
}

bool Cert::RebuildHostBundle(const std::string& host) {
  if (pem_dir_.empty() || base_ca_path_.empty()) return false;
  if (!std::filesystem::exists(base_ca_path_))   return false;

  std::error_code ec;
  const auto bundle_dir = pem_dir_ / "bundles";
  std::filesystem::create_directories(bundle_dir, ec);

  const auto bundle_path = (bundle_dir / (host + ".bundle.pem")).string();

  // Start with the base bundle
  std::ifstream in(base_ca_path_);
  if (!in) return false;

  std::string combined((std::istreambuf_iterator<char>(in)), {});
  if (combined.empty() || combined.back() != '\n') combined.push_back('\n');

  // Append any issuer PEMs we previously persisted for this host
  // Pattern: "<host>__<issuer>.pem"
  for (auto const& entry : std::filesystem::directory_iterator(pem_dir_, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    const auto name = entry.path().filename().string();

    // Naive prefix/suffix check (keeps it simple & fast)
    const std::string prefix = host + "__";
    const std::string suffix = ".pem";
    if (name.rfind(prefix, 0) == 0 && name.size() > prefix.size() + suffix.size() &&
        name.substr(name.size() - suffix.size()) == suffix) {
      std::ifstream p(entry.path());
      if (!p) continue;
      combined.append((std::istreambuf_iterator<char>(p)), {});
      if (combined.empty() || combined.back() != '\n') combined.push_back('\n');
    }
  }

  // Write out the per-host bundle (idempotent)
  std::ofstream out(bundle_path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(combined.data(), static_cast<std::streamsize>(combined.size()));
  out.flush();

  bundle_path_by_host_[host] = bundle_path;
  return true;
}

bool Cert::ApplyHostBundle(CURL* easy, const std::string& host) const {
  // If we have a cached path and it still exists, use it.
  auto it = bundle_path_by_host_.find(host);
  if (it != bundle_path_by_host_.end() && std::filesystem::exists(it->second)) {
    // Prefer CAINFO_BLOB if supported; otherwise CAINFO path
#if LIBCURL_VERSION_NUM >= 0x074700
    if (supports_cainfo_blob_) {
      std::ifstream f(it->second, std::ios::binary);
      if (f) {
        std::string blob((std::istreambuf_iterator<char>(f)), {});
        if (!blob.empty()) {
          curl_blob b{ const_cast<char*>(blob.data()), blob.size(), CURL_BLOB_COPY };
          return curl_easy_setopt(easy, CURLOPT_CAINFO_BLOB, &b) == CURLE_OK;
        }
      }
    }
#endif
    return curl_easy_setopt(easy, CURLOPT_CAINFO, it->second.c_str()) == CURLE_OK;
  }

  // Try to rebuild (e.g., first time after a new issuer was saved)
  Cert* self = const_cast<Cert*>(this); // for map update
  if (!self->RebuildHostBundle(host)) return false;

  const auto& path = bundle_path_by_host_.at(host);

#if LIBCURL_VERSION_NUM >= 0x074700
  if (supports_cainfo_blob_) {
    std::ifstream f(path, std::ios::binary);
    if (f) {
      std::string blob((std::istreambuf_iterator<char>(f)), {});
      if (!blob.empty()) {
        curl_blob b{ const_cast<char*>(blob.data()), blob.size(), CURL_BLOB_COPY };
        return curl_easy_setopt(easy, CURLOPT_CAINFO_BLOB, &b) == CURLE_OK;
      }
    }
  }
#endif
  return curl_easy_setopt(easy, CURLOPT_CAINFO, path.c_str()) == CURLE_OK;
}

// Attempt to augment CA trust for a connection with intermediates fetched via
// AIA. On success, configures libcurl to use a combined CA bundle (temp file or
// BLOB).
bool Cert::AugmentWithIntermediates(CURL* easy, const std::string& url,
                                    TempPem& hold) {
  // 1) Discover AIA URLs (with cache)
  auto aia = ExtractAiaUrls(url);
  if (aia.empty())
    return false;

  // 2) Download/convert, de-dup by issuer CN, persist new ones
  std::vector<std::string> extras;
  const std::string domain = HostFromUrl(url);

  for (const auto& issuer_url : aia) {
    if (issuer_url.rfind("ldap://", 0) == 0)
      continue;  // not supported

    std::string raw;
    if (!HttpGetToString(issuer_url, raw))
      continue;

    std::string pem = EnsurePem(raw);
    if (pem.empty())
      continue;

    const std::string issuer_cn = ExtractIssuerCNFromPem(pem);
    if (issuer_cn.empty())
      continue;

    const bool already =
        (issuer_pem_cache_.find(issuer_cn) != issuer_pem_cache_.end());
    if (!already)
      issuer_pem_cache_.emplace(issuer_cn, pem);

    if (!already) {
      // Persist new issuer PEMs to pem_dir_ if configured
      PersistPemIfConfigured(domain, issuer_cn, pem);
      extras.push_back(std::move(pem));
    }
  }

  // If we discovered nothing new, there’s nothing to apply.
  if (extras.empty())
    return false;

  // **NEW SECTION: Build or refresh the per-host bundle and apply it**
  // This replaces temp files and one-shot blobs.
  if (RebuildHostBundle(domain)) {
    if (ApplyHostBundle(easy, domain)) {
      return true;
    }
    // If applying the host bundle fails, we still fall through and try temp blob.
  }

  // --- EXISTING FALLBACK BEHAVIOR ---
  // If CAINFO_BLOB is supported and you still want to use in-memory blobs,
  // you can keep this block as a fallback:
  if (supports_cainfo_blob_ && ApplyCombinedViaBlob(easy, extras)) {
    return true;
  }

  // Otherwise, last resort: write a temporary bundle (same as before)
  std::string tmp = WriteTempBundle(extras);
  if (!tmp.empty()) {
    hold = TempPem{std::move(tmp)};  // keep alive through retry
    return curl_easy_setopt(easy, CURLOPT_CAINFO, hold.path.c_str()) ==
           CURLE_OK;
  }

  return false;
}

// Append extras to base bundle and write to a temp file; return its path.
std::string Cert::WriteTempBundle(
  const std::vector<std::string>& extra_pems) const {
  std::ifstream in(base_ca_path_);
  if (!in)
    return {};

  std::string combined((std::istreambuf_iterator<char>(in)), {});
  combined.push_back('\n');

  for (const auto& pem : extra_pems) {
    combined.append(pem);
    if (combined.back() != '\n')
      combined.push_back('\n');
  }

  // Create a temp file ending with ".pem" so mkstemps can keep suffix
  std::string tmpl =
    (std::filesystem::temp_directory_path() / "cabundle_XXXXXX.pem").string();
  // mkstemps expects a mutable char*, and returns an fd.
  std::vector<char> pathbuf(tmpl.begin(), tmpl.end());
  pathbuf.push_back('\0');
  int fd = ::mkstemps(pathbuf.data(), 4);  // ".pem" suffix length == 4
  if (fd < 0)
    return {};

  // Write and close
  FILE* fp = ::fdopen(fd, "wb");
  if (!fp) {
    ::close(fd);
    ::unlink(pathbuf.data());
    return {};
  }
  const size_t n = fwrite(combined.data(), 1, combined.size(), fp);
  (void)n;
  fclose(fp);

  return std::string(pathbuf.data());
}

// Configure libcurl to use either a CAINFO_BLOB (if available) or a temp file.
bool Cert::ApplyCombinedViaBlob(
  CURL* easy, const std::vector<std::string>& extra_pems) const {
  std::ifstream in(base_ca_path_);
  if (!in)
    return false;

  std::string combined((std::istreambuf_iterator<char>(in)), {});
  combined.push_back('\n');
  for (const auto& pem : extra_pems) {
    combined += pem;
    if (combined.empty() || combined.back() != '\n')
      combined.push_back('\n');
  }

#if LIBCURL_VERSION_NUM >= 0x074700
  curl_blob blob{const_cast<char*>(combined.data()), combined.size(),
                 CURL_BLOB_COPY};
  return curl_easy_setopt(easy, CURLOPT_CAINFO_BLOB, &blob) == CURLE_OK;
#else
  (void)easy;
  (void)extra_pems;
  return false;
#endif
}

void Cert::PersistPemIfConfigured(const std::string& domain,
                                  const std::string& issuer_cn,
                                  const std::string& pem) const {
  if (pem_dir_.empty())
    return;

  std::error_code ec;
  std::filesystem::create_directories(pem_dir_, ec);

  const auto fname = SanitizeForFilename(domain) + "__" +
                     SanitizeForFilename(issuer_cn) + ".pem";
  const auto path = (pem_dir_ / fname).string();

  std::ofstream out(path, std::ios::binary);
  if (!out)
    return;
  out.write(pem.data(), static_cast<std::streamsize>(pem.size()));
  out.flush();
}
