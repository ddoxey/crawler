#include "URL.hpp"
#include <regex>
#include <iostream>

URL::URL(const std::string& urlString) : rawUrl_(urlString) {
  Parse();
}

void URL::Parse() {
  // Simplified parser for starter purposes.
  static const std::regex urlRegex(
    // 1: optional “scheme://”   → group 1 = entire “https://”, group 2 =
    // “https” 2: host                   → group 3 3: path                   →
    // group 4 (optional) 4: query (including '?')  → group 5 (optional) 5:
    // fragment (including '#') → group 6 (optional)
    R"(^((https?)://)?([^/?#]+)(/[^?#]*)?(\?[^#]*)?(#.*)?$)",
    std::regex::extended);
  std::smatch match;
  if (std::regex_match(rawUrl_, match, urlRegex)) {
    scheme_ = match[2].matched ? match[2].str() : "";
    host_ = match[3];
    path_ = match[4].matched ? match[4].str() : "";
    query_ = match[5].matched ? match[5].str() : "";
    fragment_ = match[6].matched ? match[6].str().substr(1) : "";
  } else {
    std::cerr << "INVALID URL: " << rawUrl_ << std::endl;
  }
}

bool URL::IsValid() const {
  return scheme_.size() > 0 && host_.size();
}

void URL::ParseQueryParams() const {
  if (queryParams_.has_value())
    return;

  queryParams_.emplace();  // Initialize empty vector

  if (query_.empty() || query_[0] != '?')
    return;

  size_t start = 1;  // Skip '?'

  while (start < query_.size()) {
    size_t eqPos = query_.find('=', start);
    size_t ampPos = query_.find('&', start);

    std::string key;
    std::optional<std::string> value;

    if (eqPos != std::string::npos &&
        (ampPos == std::string::npos || eqPos < ampPos)) {
      // Case: key=value
      key = query_.substr(start, eqPos - start);

      if (ampPos != std::string::npos) {
        value = query_.substr(eqPos + 1, ampPos - eqPos - 1);
        start = ampPos + 1;
      } else {
        value = query_.substr(eqPos + 1);
        start = query_.size();
      }

    } else {
      // Case: key with no value (no '=' found before next '&' or end)
      if (ampPos != std::string::npos) {
        key = query_.substr(start, ampPos - start);
        value = std::nullopt;
        start = ampPos + 1;
      } else {
        key = query_.substr(start);
        value = std::nullopt;
        start = query_.size();
      }
    }

    // Only insert non-empty keys
    if (!key.empty()) {
      queryParams_->emplace_back(key, value);
    }
  }
}

std::string URL::GetScheme() const {
  return scheme_;
}
std::string URL::GetHost() const {
  return host_;
}
URL URL::GetDomain() const {
  std::string domain_name{host_};
  std::transform(domain_name.begin(), domain_name.end(), domain_name.begin(),
                 ::tolower);
  return URL(domain_name);
}
std::string URL::GetPath() const {
  return path_;
}

std::string URL::GetQuery() const {
  if (!queryParams_.has_value()) {
    return query_;
  }

  std::string result;
  if (!queryParams_->empty()) {
    result += '?';
    bool first = true;
    for (const auto& [key, value] : *queryParams_) {
      if (!first) {
        result += '&';
      } else {
        first = false;
      }

      result += key;

      if (value.has_value()) {
        result += '=';
        result += value.value();
      }
    }
  }

  return result;
}

std::optional<std::vector<std::optional<std::string>>> URL::GetQueryParam(
  const std::string& key) const {
  ParseQueryParams();
  std::vector<std::optional<std::string>> values;
  for (const auto& [param, value] : *queryParams_) {
    if (param == key)
      values.push_back(value);
  }
  if (values.size() == 0)
    return std::nullopt;
  return {values};
}

std::string URL::ToString() const {
  std::string url;

  if (!scheme_.empty()) {
    url += scheme_;
    url += "://";
  }

  url += host_;

  if (!path_.empty()) {
    // Make sure the path starts with '/' unless it's empty
    if (path_[0] != '/') {
      url += '/';
    }
    url += path_;
  }

  url += GetQuery();  // Will compose from queryParams if parsed, else raw query

  if (!fragment_.empty()) {
    url += '#';
    url += fragment_;
  }

  return url;
}

void URL::SetScheme(const std::string& s) {
  scheme_ = s;
}
void URL::SetHost(const std::string& h) {
  host_ = h;
}
void URL::SetPath(const std::string& p) {
  path_ = p;
}
void URL::SetQuery(const std::string& q) {
  query_ = q;
  queryParams_.reset();
}

void URL::SetQueryParam(const std::string& key,
                        std::optional<std::string> value) {
  ParseQueryParams();
  auto& vec = *queryParams_;

  auto it = std::find_if(vec.begin(), vec.end(),
                         [&](auto& kv) { return kv.first == key; });

  if (it != vec.end()) {
    // Update the first existing pair
    it->second = std::move(value);
  } else {
    // Append a new one
    vec.emplace_back(key, std::move(value));
  }
}

void URL::AppendQueryParam(const std::string& key,
                           std::optional<std::string> value) {
  ParseQueryParams();
  queryParams_->emplace_back(key, std::move(value));
}

void URL::SetFragment(const std::string& f) {
  fragment_ = f;
}
