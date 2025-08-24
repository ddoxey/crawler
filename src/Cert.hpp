#pragma once

#include <curl/curl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// ---------- TempPem ----------
// RAII for temporary PEM files created on disk; deletes on destruction.
struct TempPem {
  std::string path;

  TempPem() = default;
  explicit TempPem(std::string p) : path(std::move(p)) {
  }
  ~TempPem();

  TempPem(const TempPem&) = delete;
  TempPem& operator=(const TempPem&) = delete;

  TempPem(TempPem&&) noexcept = default;
  TempPem& operator=(TempPem&&) noexcept = default;
};

class Cert {
 public:
  struct TestHooks {
    static inline std::optional<CURLcode> force_perform_result = std::nullopt;
    static inline std::optional<std::string> fake_http_response = std::nullopt;
  };

  // Construct with optional directory for persisting PEM files.
  explicit Cert(
    const std::filesystem::path& pem_dir,
    const std::filesystem::path& ca_path = "/etc/pki/tls/certs/ca-bundle.crt")
      : pem_dir_{pem_dir}, base_ca_path_{ca_path} {
  }

  const std::filesystem::path& GetBaseCaPath() const {
    return base_ca_path_;
  }

  // Fetches additional intermediates via AIA, if needed.
  bool AugmentWithIntermediates(CURL* easy, const std::string& url,
                                TempPem& hold);

  // Extract AIA URLs from a URL's certificate (thread-safe via per-instance
  // caches).
  std::vector<std::string> ExtractAiaUrls(const std::string& url);

  // Extract PEM from CURL's certinfo (leaf cert only).
  static std::string LeafPemFromCertinfo(CURL* easy);

  // Ensure DER-encoded certs are converted to PEM; returns PEM always.
  static std::string EnsurePem(const std::string& der_or_pem);

  // SHA-1 fingerprint (hex string).
  static std::string FingerprintSha1Hex(const std::string& pem);

  // SHA-256 fingerprint (hex string).
  static std::string LeafFingerprintSha256Hex(const std::string& pem);

  // Extract issuer CN from PEM.
  static std::string ExtractIssuerCNFromPem(const std::string& pem);

  // Parse PEM for AIA "CA Issuers" URLs.
  static std::vector<std::string> AiaCaIssuersFromPem(const std::string& pem);

  // Simple HTTP GET → string helper.
  static bool HttpGetToString(const std::string& url, std::string& out);

  // Writes an on-disk CA bundle by appending extra_pems to base CA path.
  std::string WriteTempBundle(const std::vector<std::string>& extra_pems) const;

  // Apply CA bundle via CURLOPT_CAINFO_BLOB if available, else returns false.
  bool ApplyCombinedViaBlob(CURL* easy,
                            const std::vector<std::string>& extra_pems) const;

  // Persist a PEM to pem_dir_, if configured.
  void PersistPemIfConfigured(const std::string& domain,
                              const std::string& issuer_cn,
                              const std::string& pem) const;

 private:
  struct AiaCacheEntry {
    std::vector<std::string> urls;  // May be empty → "negative" cache.
    bool negative = false;
    std::chrono::steady_clock::time_point expires{};
  };

  // ---------- Per-instance state (safe with one Cert per thread) ----------
  std::filesystem::path pem_dir_;
  std::filesystem::path
    base_ca_path_{};  // System CA bundle path, set externally if needed.
  bool supports_cainfo_blob_{
    false};  // True if libcurl supports CURLOPT_CAINFO_BLOB.

  std::unordered_map<std::string, AiaCacheEntry> aia_by_host_;
  std::unordered_map<std::string, AiaCacheEntry> aia_by_fp_;
  std::unordered_map<std::string, std::string> issuer_pem_cache_;
};
