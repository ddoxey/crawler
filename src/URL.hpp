#pragma once

#include <optional>
#include <string>
#include <vector>

class URL {
 public:
  explicit URL(const std::string& url_string);

  URL& operator=(const std::string& url_string);
  URL resolve(const std::string& ref) const;

  bool IsValid() const;
  std::string GetScheme() const;
  std::string GetHost() const;
  URL GetDomain() const;
  std::string GetPath() const;
  std::string GetQuery() const;
  std::optional<std::vector<std::optional<std::string>>> GetQueryParam(
    const std::string& key) const;

  void SetScheme(const std::string& s);
  void SetHost(const std::string& h);
  void SetPath(const std::string& p);
  void SetQuery(const std::string& q);
  void SetQueryParam(const std::string& key,
                     std::optional<std::string> value = std::nullopt);
  void AppendQueryParam(const std::string& key,
                        std::optional<std::string> value = std::nullopt);
  void SetFragment(const std::string& f);

  std::string ToString() const;
  std::string GetSha256() const;

  // Equality‐operator: two URLs are “equal” if their canonical string forms
  // match
  bool operator==(const URL& other) const {
    return ToString() == other.ToString();
  }
  bool operator!=(const URL& other) const {
    return !(*this == other);
  }
  bool operator<(URL const& other) const {
    return ToString() < other.ToString();
  }

 private:
  std::string rawUrl_;
  std::string scheme_, host_, path_, query_, fragment_;
  mutable std::string sha256_;
  mutable std::optional<
    std::vector<std::pair<std::string, std::optional<std::string>>>>
    queryParams_;

  void Parse();
  void ParseQueryParams() const;
};

/// Free‐function overload of operator<< so you can do:
///    std::cout << myUrlObj << "\n";
inline std::ostream& operator<<(std::ostream& os, const URL& u) {
  os << u.ToString();
  return os;
}

namespace std {
/// std::hash<URL> specialization so you can
/// use URL in unordered_map/set
template <>
struct hash<URL> {
  size_t operator()(const URL& u) const noexcept {
    // Simple: hash the canonical string form
    return std::hash<std::string>()(u.ToString());
  }
};
}  // namespace std
