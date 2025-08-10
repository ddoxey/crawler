#include <gtest/gtest.h>
#include "URL.hpp"

TEST(URLTest, BasicParsing) {
  URL url("http://example.com/path?foo=bar");

  EXPECT_EQ(url.GetScheme(), "http");
  EXPECT_EQ(url.GetHost(), "example.com");
  EXPECT_EQ(url.GetPath(), "/path");
  EXPECT_EQ(url.GetQuery(), "?foo=bar");
}

TEST(URLTest, MissingPathAndQuery) {
  URL url("https://anotherdomain.org");

  EXPECT_EQ(url.GetScheme(), "https");
  EXPECT_EQ(url.GetHost(), "anotherdomain.org");
  EXPECT_EQ(url.GetPath(), "");
  EXPECT_EQ(url.GetQuery(), "");
}

TEST(URLTest, ComplexURL) {
  URL url("https://sub.example.com/some/page?x=1&y=2");

  EXPECT_EQ(url.GetScheme(), "https");
  EXPECT_EQ(url.GetHost(), "sub.example.com");
  EXPECT_EQ(url.GetPath(), "/some/page");
  EXPECT_EQ(url.GetQuery(), "?x=1&y=2");
}

TEST(URLTest, SingleParam) {
  URL url("http://example.com/page?foo=bar");

  auto fooOpt = url.GetQueryParam("foo");
  ASSERT_TRUE(fooOpt.has_value()) << "Expected 'foo' to be present";
  const auto& foo = *fooOpt;

  ASSERT_EQ(foo.size(), 1);
  ASSERT_TRUE(foo[0].has_value());
  EXPECT_EQ(foo[0].value(), "bar");
}

TEST(URLTest, MultipleParams) {
  URL url("http://example.com/page?foo=bar&baz=qux&empty=");

  auto fooOpt = url.GetQueryParam("foo");
  ASSERT_TRUE(fooOpt.has_value());
  {
    const auto& foo = *fooOpt;
    ASSERT_EQ(foo.size(), 1);
    EXPECT_EQ(foo[0].value(), "bar");
  }

  auto bazOpt = url.GetQueryParam("baz");
  ASSERT_TRUE(bazOpt.has_value());
  {
    const auto& baz = *bazOpt;
    ASSERT_EQ(baz.size(), 1);
    EXPECT_EQ(baz[0].value(), "qux");
  }

  auto emptyOpt = url.GetQueryParam("empty");
  ASSERT_TRUE(emptyOpt.has_value());
  {
    const auto& empty = *emptyOpt;
    ASSERT_EQ(empty.size(), 1);
    ASSERT_TRUE(empty[0].has_value());
    EXPECT_EQ(empty[0].value(), "");
  }
}

TEST(URLTest, MissingParam) {
  URL url("http://example.com/page?foo=bar");

  auto missingOpt = url.GetQueryParam("doesnotexist");
  EXPECT_FALSE(missingOpt.has_value());
}

TEST(URLTest, EmptyQueryString) {
  URL url("http://example.com/page");

  auto fooOpt = url.GetQueryParam("foo");
  EXPECT_FALSE(fooOpt.has_value());
}

TEST(URLTest, NoValueParameter) {
  // 'flag' present with no '=', 'foo' as normal
  URL url("http://example.com/page?flag&foo=bar");

  auto flagOpt = url.GetQueryParam("flag");
  ASSERT_TRUE(flagOpt.has_value());
  {
    const auto& flag = *flagOpt;
    ASSERT_EQ(flag.size(), 1);
    EXPECT_FALSE(flag[0].has_value());
  }

  auto fooOpt = url.GetQueryParam("foo");
  ASSERT_TRUE(fooOpt.has_value());
  {
    const auto& foo = *fooOpt;
    ASSERT_EQ(foo.size(), 1);
    EXPECT_EQ(foo[0].value(), "bar");
  }
}

TEST(URLTest, DuplicateKeys) {
  URL url("http://example.com/page?x=1&x=2&x");

  auto xOpt = url.GetQueryParam("x");
  ASSERT_TRUE(xOpt.has_value());
  const auto& x = *xOpt;

  ASSERT_EQ(x.size(), 3);
  ASSERT_TRUE(x[0].has_value());
  EXPECT_EQ(x[0].value(), "1");
  ASSERT_TRUE(x[1].has_value());
  EXPECT_EQ(x[1].value(), "2");
  EXPECT_FALSE(x[2].has_value());
}

TEST(URLTest, ToStringReflectsChanges) {
  URL url("http://example.com/path?foo=bar");

  // original
  EXPECT_EQ(url.ToString(), "http://example.com/path?foo=bar");

  url.SetScheme("https");
  EXPECT_EQ(url.ToString(), "https://example.com/path?foo=bar");

  url.SetHost("newdomain.org");
  EXPECT_EQ(url.ToString(), "https://newdomain.org/path?foo=bar");

  url.SetPath("/newpath");
  EXPECT_EQ(url.ToString(), "https://newdomain.org/newpath?foo=bar");
}

TEST(URLTest, SimpleCom) {
  URL url("https://a.b.example.com/path");
  EXPECT_EQ(url.GetPublicSuffix(), "com");
  EXPECT_EQ(url.GetSecondLevelDomain(), "example");
  EXPECT_EQ(url.GetRegistrableDomain(), "example.com");
  auto subs = url.GetSubdomains();
  ASSERT_EQ(subs.size(), 2);
  EXPECT_EQ(subs[0], "a");
  EXPECT_EQ(subs[1], "b");
}

TEST(URLTest, UKPublicSuffix) {
  URL url("https://sub.example.co.uk/");
  EXPECT_EQ(url.GetPublicSuffix(), "co.uk");
  EXPECT_EQ(url.GetSecondLevelDomain(), "example");
  EXPECT_EQ(url.GetRegistrableDomain(), "example.co.uk");
  auto subs = url.GetSubdomains();
  ASSERT_EQ(subs.size(), 1);
  EXPECT_EQ(subs[0], "sub");
}

TEST(URLTest, AUAndDeepSubs) {
  URL url("https://x.y.z.company.com.au/");
  EXPECT_EQ(url.GetPublicSuffix(), "com.au");
  EXPECT_EQ(url.GetSecondLevelDomain(), "company");
  EXPECT_EQ(url.GetRegistrableDomain(), "company.com.au");
  auto subs = url.GetSubdomains();
  std::vector<std::string> expected{"x", "y", "z"};
  EXPECT_EQ(subs, expected);
}

TEST(URLTest, IPv4AndIPv6) {
  URL v4("http://127.0.0.1/path");
  EXPECT_TRUE(v4.HostIsIPv4());
  EXPECT_FALSE(v4.HostIsIPv6());
  EXPECT_EQ(v4.GetPublicSuffix(), "");
  EXPECT_EQ(v4.GetRegistrableDomain(), "127.0.0.1");

  URL v6("http://[2001:db8::1]/");
  EXPECT_FALSE(v6.HostIsIPv4());
  EXPECT_TRUE(v6.HostIsIPv6());
  EXPECT_EQ(v6.GetPublicSuffix(), "");
  EXPECT_EQ(v6.GetRegistrableDomain(), "[2001:db8::1]");
}

TEST(URLTest, MixedCaseHost) {
  URL url("https://SuB.ExAmPlE.CoM/");
  EXPECT_EQ(url.GetPublicSuffix(), "com");
  EXPECT_EQ(url.GetRegistrableDomain(), "example.com");
}
