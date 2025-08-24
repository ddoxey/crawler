#include "Cert.hpp"

#include <sstream>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace fs = std::filesystem;

// --- Test helper: generate a real, tiny self-signed PEM (EC P-256,
// CN=IssuerName) ---
static std::string MakeSelfSignedPem(const std::string& cn = "IssuerName") {
  std::string pem;
  EVP_PKEY* pkey = nullptr;
  X509* x = nullptr;
  EC_KEY* eckey = nullptr;

  auto cleanup = [&]() {
    if (x)
      X509_free(x);
    if (pkey)
      EVP_PKEY_free(pkey);
    if (eckey)
      EC_KEY_free(eckey);
  };

  // Generate EC key (prime256v1)
  eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (!eckey) {
    cleanup();
    return {};
  }
  if (EC_KEY_generate_key(eckey) != 1) {
    cleanup();
    return {};
  }

  pkey = EVP_PKEY_new();
  if (!pkey || EVP_PKEY_assign_EC_KEY(pkey, eckey) != 1) {
    cleanup();
    return {};
  }
  // pkey owns eckey now
  eckey = nullptr;

  x = X509_new();
  if (!x) {
    cleanup();
    return {};
  }

  // Serial = 1
  ASN1_INTEGER_set(X509_get_serialNumber(x), 1);

  // Validity: now .. now + 365 days
  X509_gmtime_adj(X509_get_notBefore(x), 0);
  X509_gmtime_adj(X509_get_notAfter(x), 60 * 60 * 24 * 365);

  // Public key
  if (X509_set_pubkey(x, pkey) != 1) {
    cleanup();
    return {};
  }

  // Subject and Issuer (self-signed)
  X509_NAME* name = X509_NAME_new();
  if (!name) {
    cleanup();
    return {};
  }
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>(cn.c_str()),
                             -1, -1, 0);
  X509_set_subject_name(x, name);
  X509_set_issuer_name(x, name);
  X509_NAME_free(name);

  // Sign (sha256)
  if (X509_sign(x, pkey, EVP_sha256()) == 0) {
    cleanup();
    return {};
  }

  // Write to PEM string
  BIO* mem = BIO_new(BIO_s_mem());
  if (!mem) {
    cleanup();
    return {};
  }
  PEM_write_bio_X509(mem, x);
  BUF_MEM* bptr = nullptr;
  BIO_get_mem_ptr(mem, &bptr);
  if (bptr && bptr->data && bptr->length) {
    pem.assign(bptr->data, bptr->length);
  }
  BIO_free(mem);
  cleanup();
  return pem;
}

const std::string kDummyPem = MakeSelfSignedPem("IssuerName");

// Helper: write a PEM file with known content
static fs::path WritePemFile(const std::string& pem, const fs::path& dir,
                             const std::string& name) {
  fs::create_directories(dir);
  auto path = dir / name;
  std::ofstream out(path);
  out << pem;
  out.close();
  return path;
}

class CertTest : public ::testing::Test {
 protected:
  fs::path tmpdir;

  void SetUp() override {
    tmpdir = fs::temp_directory_path() / "cert_test";
    fs::remove_all(tmpdir);
    fs::create_directories(tmpdir);
  }

  void TearDown() override {
    fs::remove_all(tmpdir);
  }
};

TEST_F(CertTest, EnsurePemPassThrough) {
  SCOPED_TRACE(
    "Verifies EnsurePem returns the input unchanged when it is already "
    "PEM-formatted.");
  RecordProperty("description", "EnsurePem should be a no-op for PEM input.");
  auto pem = Cert::EnsurePem(kDummyPem);
  EXPECT_EQ(pem, kDummyPem);
}

TEST_F(CertTest, FingerprintSha1HexWorks) {
  SCOPED_TRACE(
    "Computes SHA-1 fingerprint of a valid PEM and returns 40 hex chars.");
  RecordProperty(
    "description",
    "FingerprintSha1Hex produces a 20-byte digest (40 hex chars).");
  auto fp = Cert::FingerprintSha1Hex(kDummyPem);
  EXPECT_FALSE(fp.empty());
  EXPECT_EQ(fp.size(), 40) << "SHA-1 should be 20 bytes → 40 hex chars";
}

TEST_F(CertTest, FingerprintSha256HexWorks) {
  SCOPED_TRACE(
    "Computes SHA-256 fingerprint of a valid PEM and returns 64 hex chars.");
  RecordProperty(
    "description",
    "LeafFingerprintSha256Hex produces a 32-byte digest (64 hex chars).");
  auto fp = Cert::LeafFingerprintSha256Hex(kDummyPem);
  EXPECT_FALSE(fp.empty());
  EXPECT_EQ(fp.size(), 64) << "SHA-256 should be 32 bytes → 64 hex chars";
}

TEST_F(CertTest, ExtractIssuerCN) {
  SCOPED_TRACE("Extracts the issuer Common Name (CN) from a valid PEM.");
  RecordProperty("description",
                 "ExtractIssuerCNFromPem should return the CN used to generate "
                 "the PEM (IssuerName).");
  auto cn = Cert::ExtractIssuerCNFromPem(kDummyPem);
  EXPECT_EQ(cn, "IssuerName") << "Issuer CN should match dummy PEM CN";
}

TEST_F(CertTest, PersistPemIfConfigured) {
  SCOPED_TRACE(
    "Persists a PEM to the configured directory with a sanitized filename.");
  RecordProperty("description",
                 "PersistPemIfConfigured writes 'domain__issuer.pem' to the "
                 "provided directory.");
  Cert cert(tmpdir);
  cert.PersistPemIfConfigured("example.com", "Test CA", kDummyPem);

  auto expected_path = tmpdir / "example.com__Test_CA.pem";
  ASSERT_TRUE(fs::exists(expected_path)) << "PEM file should be persisted";
  std::ifstream in(expected_path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  EXPECT_EQ(buffer.str(), kDummyPem);
}

TEST_F(CertTest, AiaCaIssuersFromPemHandlesNoAIA) {
  SCOPED_TRACE(
    "Parses AIA 'CA Issuers' URLs from PEM; returns empty when AIA is absent.");
  RecordProperty("description",
                 "A PEM without AUTHORITY_INFO_ACCESS should yield an empty CA "
                 "Issuers list.");
  auto urls = Cert::AiaCaIssuersFromPem(kDummyPem);
  EXPECT_TRUE(urls.empty())
    << "Dummy PEM has no AIA extension → expect empty list";
}

TEST_F(CertTest, HttpGetToStringFailsGracefully) {
  SCOPED_TRACE(
    "Graceful failure path for HTTP fetch: invalid URL returns false and no "
    "body.");
  RecordProperty("description",
                 "HttpGetToString should return false and leave output empty "
                 "on invalid endpoints.");
  std::string data;
  // Invalid URL should return false quickly
  EXPECT_FALSE(Cert::HttpGetToString("http://127.0.0.1:0", data));
  EXPECT_TRUE(data.empty());
}

TEST_F(CertTest, ExtractAiaUrlsCachesNegativeResults) {
  SCOPED_TRACE(
    "Calls ExtractAiaUrls twice for the same host and expects identical "
    "results due to per-instance caching.");
  RecordProperty("description",
                 "ExtractAiaUrls should cache results (positive or negative) "
                 "for host/fingerprint.");
  Cert cert(tmpdir);
  // Using a random URL that will not provide AIA URLs in cert
  auto urls1 = cert.ExtractAiaUrls("https://example.com");
  auto urls2 = cert.ExtractAiaUrls("https://example.com");

  // Either empty (negative) or non-empty (if system cert has AIA)
  EXPECT_EQ(urls1, urls2);
}

TEST_F(CertTest, AugmentWithIntermediatesFailsWithoutAIA) {
  SCOPED_TRACE(
    "Uses reserved TLD '.invalid' to avoid DNS and verify no intermediates are "
    "added.");
  RecordProperty("description",
                 "AugmentWithIntermediates should return false when AIA cannot "
                 "be discovered/downloaded.");
  Cert cert(tmpdir);
  CURL* curl = curl_easy_init();
  ASSERT_NE(curl, nullptr);

  TempPem hold;
  // Likely no AIA on dummy URL → expect false
  EXPECT_FALSE(
    cert.AugmentWithIntermediates(curl, "https://domain.invalid", hold));
  curl_easy_cleanup(curl);
}

TEST_F(CertTest, AugmentWithIntermediatesOfflineFalse) {
  SCOPED_TRACE(
    "Stubs networking to simulate success with empty responses so no "
    "intermediates can be extracted.");
  RecordProperty("description",
                 "With forced CURL OK and empty downloads, augmentation should "
                 "still return false.");
  Cert cert(tmpdir);
  CURL* curl = curl_easy_init();
  ASSERT_NE(curl, nullptr);

  // Make HEAD “succeed” but provide no issuer PEMs
  Cert::TestHooks::force_perform_result = CURLE_OK;
  Cert::TestHooks::fake_http_response =
    "";  // Ensure EnsurePem() fails → no extras

  TempPem hold;
  EXPECT_FALSE(cert.AugmentWithIntermediates(curl, "https://any.host", hold));

  Cert::TestHooks::force_perform_result.reset();
  Cert::TestHooks::fake_http_response.reset();
  curl_easy_cleanup(curl);
}

TEST_F(CertTest, WriteTempBundleCreatesCombinedPemFile) {
  SCOPED_TRACE(
    "Creates a temporary CA bundle that appends extra PEMs and returns a valid "
    "path.");
  RecordProperty("description",
                 "WriteTempBundle should produce a readable PEM file "
                 "containing cert material.");
  Cert cert(tmpdir);

  // Write bundle combining base PEM + extras
  auto path = cert.WriteTempBundle({kDummyPem});
  ASSERT_FALSE(path.empty());
  ASSERT_TRUE(fs::exists(path));

  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  auto contents = buffer.str();
  EXPECT_NE(contents.find("BEGIN CERTIFICATE"), std::string::npos);
}

TEST_F(CertTest, ApplyCombinedViaBlobVersionAware) {
  SCOPED_TRACE(
    "Verifies CA bundle application via CURLOPT_CAINFO_BLOB when supported by "
    "libcurl.");
  RecordProperty("description",
                 "ApplyCombinedViaBlob should succeed on libcurl >= 7.71.0; "
                 "otherwise return false gracefully.");
  Cert cert(tmpdir);
  CURL* curl = curl_easy_init();
  ASSERT_NE(curl, nullptr);

#if LIBCURL_VERSION_NUM >= 0x074700
  EXPECT_TRUE(cert.ApplyCombinedViaBlob(curl, {kDummyPem}));
#else
  EXPECT_FALSE(cert.ApplyCombinedViaBlob(curl, {kDummyPem}));
#endif

  curl_easy_cleanup(curl);
}

TEST_F(CertTest, EnsurePemConvertsDerToPem) {
  SCOPED_TRACE(
    "Converts DER-encoded certificate bytes to PEM and preserves "
    "parseability.");
  RecordProperty("description",
                 "EnsurePem should detect DER input, produce PEM, and keep "
                 "issuer CN readable.");
  // Make a real self-signed X509
  const std::string pem = MakeSelfSignedPem("IssuerName");
  ASSERT_FALSE(pem.empty());

  // Read back to X509, then export DER
  BIO* bio = BIO_new_mem_buf(pem.data(), (int)pem.size());
  ASSERT_NE(bio, nullptr);
  X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  ASSERT_NE(x, nullptr);

  unsigned char* der = nullptr;
  int der_len = i2d_X509(x, &der);
  X509_free(x);
  ASSERT_GT(der_len, 0);
  std::string der_blob(reinterpret_cast<char*>(der), der_len);
  OPENSSL_free(der);

  // EnsurePem should convert DER → PEM
  const std::string converted = Cert::EnsurePem(der_blob);
  ASSERT_FALSE(converted.empty());
  EXPECT_NE(converted.find("-----BEGIN CERTIFICATE-----"), std::string::npos);

  // Bonus: issuer CN still parses correctly
  EXPECT_EQ(Cert::ExtractIssuerCNFromPem(converted), "IssuerName");
}
