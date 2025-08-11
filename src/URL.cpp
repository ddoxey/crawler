#include "URL.hpp"
#include "Logger.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <openssl/sha.h>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace {
// Minimal seed; you can extend or load from file at startup.
// Keep them lowercase and left-normalized.
const std::unordered_set<std::string> kMultiLabelPublicSuffixes = {
  "co.uk",  "ac.uk",  "gov.uk", "org.uk",  "sch.uk", "com.au", "net.au",
  "org.au", "edu.au", "gov.au", "co.jp",   "ne.jp",  "or.jp",  "ac.jp",
  "go.jp",  "co.nz",  "org.nz", "govt.nz", "ac.nz",  "com.br", "net.br",
  "org.br", "gov.br", "com.cn", "net.cn",  "org.cn", "gov.cn"};

inline bool is_ipv6_literal(const std::string& host) {
  return !host.empty() && host.front() == '[' && host.back() == ']';
}

inline bool is_ipv4(const std::string& host) {
  // very light check – enough to avoid “dot-splitting” names that are actually
  // IPv4
  unsigned a, b, c, d;
  char dot;
  std::stringstream ss(host);
  return (ss >> a >> dot >> b >> dot >> c >> dot >> d) && dot == '.';
}

std::vector<std::string> split_labels(const std::string& host_lc) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    auto dot = host_lc.find('.', start);
    out.emplace_back(host_lc.substr(
      start, dot == std::string::npos ? std::string::npos : dot - start));
    if (dot == std::string::npos)
      break;
    start = dot + 1;
  }
  return out;
}

// returns length (in labels) of the public suffix: 1 for "com", 2 for "co.uk",
// etc.
size_t public_suffix_len(const std::string& host_lc) {
  // IPv6/IPv4: no label-based TLD semantics
  if (is_ipv6_literal(host_lc) || is_ipv4(host_lc))
    return 0;

  // Try multi-label list first
  // Check "co.uk", "com.au", etc. (suffix matches end of host)
  for (const auto& ps : kMultiLabelPublicSuffixes) {
    if (host_lc.size() >= ps.size() &&
        host_lc.compare(host_lc.size() - ps.size(), ps.size(), ps) == 0) {
      // Ensure whole-label match: char before must be '.'
      if (host_lc.size() == ps.size() ||
          host_lc[host_lc.size() - ps.size() - 1] == '.')
        return std::count(ps.begin(), ps.end(), '.') + 1;
    }
  }

  // Fallback: last label is the TLD (".com", ".org", …)
  auto labels = split_labels(host_lc);
  return labels.size() >= 1 ? 1 : 0;
}

// helper: join and normalize “/a/b/../c” → “/a/c”
static std::string normalize_path(const std::string& raw) {
  std::vector<std::string> parts;
  for (size_t i = 0, n = raw.size(); i < n;) {
    // split on '/'
    size_t j = raw.find('/', i);
    if (j == std::string::npos)
      j = n;
    std::string seg = raw.substr(i, j - i);
    if (seg == "..") {
      if (!parts.empty())
        parts.pop_back();
    } else if (seg != "" && seg != ".") {
      parts.push_back(seg);
    }
    i = j + 1;
  }
  std::string out = "/";
  for (size_t k = 0; k < parts.size(); ++k) {
    out += parts[k];
    if (k + 1 < parts.size())
      out += "/";
  }
  return out;
}
}  // namespace

URL::URL(const std::string& urlString) : rawUrl_(urlString) {
  Parse();
}

URL& URL::operator=(const std::string& urlString) {
  if (urlString.find("://") != std::string::npos) {
    // absolute URL: assign and reparse directly
    rawUrl_ = urlString;
    queryParams_.reset();
    Parse();
  } else {
    // relative URL: resolve against current, then assign via copy‐assign
    URL resolved = this->Resolve(urlString);
    *this = resolved;  // invokes the default URL& operator=(const URL&)
  }
  return *this;
}

URL URL::Resolve(const URL& ref) const {
  return Resolve(ref.ToString());
}

URL URL::Resolve(const std::string& ref) const {
  // Absolute: scheme present
  if (ref.find("://") != std::string::npos) {
    return URL(ref);
  }

  // Protocol-relative: inherit base scheme
  if (ref.size() >= 2 && ref[0] == '/' && ref[1] == '/') {
    return URL(scheme_ + ":" + ref);
  }

  // Split ref into path, query, fragment
  std::string_view sv = ref;
  std::string frag, ref_query, ref_path;

  if (auto h = sv.find('#'); h != std::string::npos) {
    frag.assign(sv.substr(h + 1));
    sv = sv.substr(0, h);
  }
  if (auto q = sv.find('?'); q != std::string::npos) {
    ref_query.assign(sv.substr(q));  // keep leading '?'
    ref_path.assign(sv.substr(0, q));
  } else {
    ref_path.assign(sv);
  }

  // Origin from parsed fields (never includes query/fragment)
  const std::string origin = scheme_.empty() ? "" : (scheme_ + "://" + host_);

  // Compute path
  std::string path;
  if (ref_path.empty()) {
    // Empty relative path inherits the base path
    path = path_.empty() ? "/" : path_;
  } else if (ref_path[0] == '/') {
    path = normalize_path(ref_path);  // absolute path ref
  } else {
    // Relative to base directory
    const std::string base_dir =
      path_.empty() ? "/" : path_.substr(0, path_.find_last_of('/') + 1);
    path = normalize_path(base_dir + ref_path);
  }

  // Query: ref wins; else inherit only when path is empty
  const std::string query = !ref_query.empty() ? ref_query
                            : ref_path.empty() ? query_
                                               : "";

  // Assemble (you can also add a private ctor to skip re-Parse)
  return URL(origin + path + query + (frag.empty() ? "" : "#" + frag));
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
    sha256_.clear();
  } else {
    logr::warning << "INVALID URL: " << rawUrl_;
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
  auto domain = GetRegistrableDomain();
  return URL(domain);
}

std::string URL::GetPublicSuffix() const {
  std::string host_lc = host_;
  std::transform(host_lc.begin(), host_lc.end(), host_lc.begin(), ::tolower);
  if (is_ipv6_literal(host_lc) || is_ipv4(host_lc))
    return "";
  auto labels = split_labels(host_lc);
  size_t ps_len = public_suffix_len(host_lc);
  if (ps_len == 0 || ps_len > labels.size())
    return "";
  // Join the last ps_len labels
  std::string out;
  for (size_t i = labels.size() - ps_len; i < labels.size(); ++i) {
    if (!out.empty())
      out += '.';
    out += labels[i];
  }
  return out;
}

std::string URL::GetRegistrableDomain() const {
  std::string host_lc = host_;
  std::transform(host_lc.begin(), host_lc.end(), host_lc.begin(), ::tolower);
  if (is_ipv6_literal(host_lc) || is_ipv4(host_lc))
    return host_lc;  // treat as whole
  auto labels = split_labels(host_lc);
  size_t ps_len = public_suffix_len(host_lc);
  if (ps_len == 0 || labels.size() <= ps_len)
    return "";  // no registrable part
  // eTLD+1: one label left of public suffix + the public suffix
  size_t start = labels.size() - (ps_len + 1);
  std::string out = labels[start];
  for (size_t i = start + 1; i < labels.size(); ++i) {
    out += '.';
    out += labels[i];
  }
  return out;
}

std::vector<std::string> URL::GetSubdomains() const {
  std::vector<std::string> result;
  std::string host_lc = host_;
  std::transform(host_lc.begin(), host_lc.end(), host_lc.begin(), ::tolower);
  if (is_ipv6_literal(host_lc) || is_ipv4(host_lc))
    return result;
  auto labels = split_labels(host_lc);
  size_t ps_len = public_suffix_len(host_lc);
  if (ps_len == 0)
    return result;
  // registrable domain consumes 1 + ps_len labels at the end
  if (labels.size() <= ps_len + 1)
    return result;
  result.assign(labels.begin(), labels.end() - (ps_len + 1));  // left→right
  return result;
}

std::string URL::GetSecondLevelDomain() const {
  // the label immediately left of the public suffix
  std::string host_lc = host_;
  std::transform(host_lc.begin(), host_lc.end(), host_lc.begin(), ::tolower);
  if (is_ipv6_literal(host_lc) || is_ipv4(host_lc))
    return "";
  auto labels = split_labels(host_lc);
  size_t ps_len = public_suffix_len(host_lc);
  if (ps_len == 0 || labels.size() <= ps_len)
    return "";
  return labels[labels.size() - (ps_len + 1)];
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

std::string URL::GetSha256() const {
  if (sha256_.empty()) {
    auto url = ToString();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)url.c_str(), url.size(), hash);
    std::ostringstream oss;
    for (auto byte : hash) {
      oss << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
    }
    sha256_ = oss.str();
  }
  return sha256_;
}

bool URL::HostIsIPv4() const {
  return is_ipv4(host_);
}
bool URL::HostIsIPv6() const {
  return is_ipv6_literal(host_);
}
