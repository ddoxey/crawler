#pragma once

#include "URL.hpp"
#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <optional>

class HttpResponse {
 public:
  HttpResponse() = default;

  /// Parse one raw header line (e.g. "Content-Type: text/html")
  void AddHeaderLine(const std::string& line);

  /// Append to the response body
  void AppendBody(const char* data, size_t len);

  /// Return the first header value matching `key` (case‑insensitive)
  std::optional<std::string> GetHeader(const std::string& key) const;

  /// Return all header values matching `key` (case‑insensitive)
  std::vector<std::string> GetHeaders(const std::string& key) const;

  /// The accumulated body text
  const std::string& GetBody() const;

  /// All parsed header (name,value) pairs in order received
  const std::vector<std::pair<std::string, std::string>>& GetHeaders() const;

  /// Set the HTTP status code
  void SetStatusCode(long http_status);

  /// Set the redirect count
  void SetRedirectCount(long c);

  /// Set the effective URL (after any redirects)
  void SetEffectiveUrl(const std::string url);

  /// Get the number of redirects
  long GetRedirectCount() const;

  /// Get the effective URL
  const URL& GetEffectiveUrl() const;

  /// HTTP status code is 200 to 299
  const bool IsOkay() const;

  /// HTTP status code is 300 to 399
  const bool IsRedirect() const;

 private:
  std::vector<std::pair<std::string, std::string>> headers_;
  std::string body_;
  long status_code_{0};
  long redirect_count_{0};
  std::unique_ptr<URL> effective_url_;
};
