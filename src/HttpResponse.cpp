#include "HttpResponse.hpp"
#include <algorithm>
#include <cctype>

void HttpResponse::AddHeaderLine(const std::string& line) {
  auto colon = line.find(':');
  if (colon == std::string::npos)
    return;  // skip non‑header lines

  std::string name = line.substr(0, colon);
  std::string value = line.substr(colon + 1);

  // trim leading/trailing whitespace
  auto trim = [](std::string& s) {
    auto l = s.find_first_not_of(" \t\r\n");
    auto r = s.find_last_not_of(" \t\r\n");
    if (l == std::string::npos) {
      s.clear();
      return;
    }
    s = s.substr(l, r - l + 1);
  };
  trim(name);
  trim(value);

  headers_.emplace_back(std::move(name), std::move(value));
}

void HttpResponse::AppendBody(const char* data, size_t len) {
  body_.append(data, len);
}

std::optional<std::string> HttpResponse::GetHeader(
  const std::string& key) const {
  auto lowercase = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  std::string want = lowercase(key);
  for (auto const& [name, val] : headers_) {
    if (lowercase(name) == want) {
      return val;
    }
  }
  return std::nullopt;
}

std::vector<std::string> HttpResponse::GetHeaders(
  const std::string& key) const {
  std::vector<std::string> out;
  auto lowercase = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  std::string want = lowercase(key);
  for (auto const& [name, val] : headers_) {
    if (lowercase(name) == want) {
      out.push_back(val);
    }
  }
  return out;
}

const std::vector<std::pair<std::string, std::string>>&
HttpResponse::GetHeaders() const {
  return headers_;
}

const std::string& HttpResponse::GetBody() const {
  return body_;
}

void HttpResponse::SetStatusCode(long http_status) {
  status_code_ = http_status;
  headers_.emplace_back("X-HTTP-Status", std::to_string(status_code_));
}

const bool HttpResponse::IsOkay() const {
  return status_code_ >= 200 && status_code_ < 300;
}

const bool HttpResponse::IsRedirect() const {
  return status_code_ >= 300 && status_code_ < 400;
}
