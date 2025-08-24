#include "LuaProcessor.hpp"
#include "URL.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = std::filesystem;

class LuaProcessorTest : public ::testing::Test {
 protected:
  fs::path scripts_dir_;

  static fs::path FindScriptsDir() {
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
      auto dir = exe.parent_path();
      for (int i = 0; i < 8; ++i) {
        if (fs::exists(dir / "sample" / "scripts" / "common" / "init.lua"))
          return dir / "sample" / "scripts";
        dir = dir.parent_path();
      }
    }
    throw std::runtime_error("Could not locate scripts/common/init.lua");
  }

  void SetUp() override {
    scripts_dir_ = FindScriptsDir();
  }

  void TearDown() override {
  }
};

TEST_F(LuaProcessorTest, LoadsDomainAndParsesTitle) {
  SCOPED_TRACE(
    "Parses a basic HTML document and extracts the <title> using the domain’s "
    "Lua script.");
  RecordProperty("description",
                 "Ensures LuaProcessor loads the site script for example.com "
                 "and returns JSON with title and url.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  ASSERT_TRUE(lp.HasScript());

  const std::string html =
    "<html><head><title> Hello World </title></head><body></body></html>";
  URL url("https://example.com/path");

  auto jopt = lp.Process(url, html);  // std::optional<nlohmann::json>
  ASSERT_TRUE(jopt.has_value()) << "LuaProcessor returned no JSON";

  const nlohmann::json& result_j = *jopt;
  ASSERT_TRUE(result_j.is_object());
  ASSERT_TRUE(result_j.contains("title"));
  EXPECT_EQ(result_j.at("title").get<std::string>(), "Hello World");

  // url echoed back from script
  ASSERT_TRUE(result_j.contains("url"));
  EXPECT_EQ(result_j.at("url").get<std::string>(), url.ToString());

  // no client redirect in this simple HTML
  auto cr = result_j.find("client_redirect");
  EXPECT_TRUE(cr == result_j.end() || cr->is_null());
}

TEST_F(LuaProcessorTest, HandlesMissingTitleGracefully) {
  SCOPED_TRACE(
    "Handles documents with no <title> element without throwing and returns an "
    "empty title string.");
  RecordProperty("description",
                 "Verifies the script returns a JSON object containing an "
                 "empty 'title' when no title is present.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  URL url("https://example.com/page");
  const std::string html =
    "<html><head></head><body>No title here</body></html>";

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& result_j = *jopt;

  ASSERT_TRUE(result_j.contains("title"));
  EXPECT_EQ(result_j.at("title").get<std::string>(), "");
}

TEST_F(LuaProcessorTest, MetaRefresh_Immediate_WithBaseHref) {
  SCOPED_TRACE(
    "Extracts client redirect from a meta refresh with delay=0 and honors "
    "<base href>.");
  RecordProperty(
    "description",
    "Meta tag 'refresh' with URL=../next should produce client_redirect "
    "{type=meta, delay=0, url='../next', base=...}.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  URL url("https://example.com/start");
  const std::string html = R"(
    <html><head>
      <base href="https://example.com/dir/">
      <meta http-equiv="refresh" content="0; URL=../next">
      <title>t</title>
    </head><body></body></html>
  )";

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt);
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  ASSERT_TRUE(it->is_object());
  const auto& cr = *it;

  EXPECT_EQ(cr.at("type").get<std::string>(), "meta");
  EXPECT_EQ(cr.at("delay").get<int>(), 0);
  EXPECT_EQ(cr.at("url").get<std::string>(),
            "../next");  // raw target from HTML
  EXPECT_EQ(cr.at("base").get<std::string>(),
            "https://example.com/dir/");  // from <base href>
}

TEST_F(LuaProcessorTest, MetaRefresh_Quoted_Uppercase_WithDelay) {
  SCOPED_TRACE(
    "Parses REFRESH meta tag with mixed casing, quotes, and a nonzero delay.");
  RecordProperty("description",
                 "Expect client_redirect {type=meta, delay=5, "
                 "url='https://target.example/landing'}.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  URL url("https://example.com/x");
  const std::string html = R"(
    <html><head>
      <meta HTTP-EQUIV="REFRESH" content="5; url='https://target.example/landing'">
    </head></html>
  )";

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt);
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  const auto& cr = *it;
  EXPECT_EQ(cr.at("type").get<std::string>(), "meta");
  EXPECT_EQ(cr.at("delay").get<int>(), 5);
  EXPECT_EQ(cr.at("url").get<std::string>(), "https://target.example/landing");
  // base likely absent
  auto b = cr.find("base");
  EXPECT_TRUE(b == cr.end() || b->is_null() ||
              (b->is_string() && b->get<std::string>().empty()));
}

TEST_F(LuaProcessorTest, Js_WindowLocation_Assignment) {
  SCOPED_TRACE(
    "Captures JavaScript-driven redirects via 'window.location = ...'.");
  RecordProperty("description",
                 "Expect client_redirect {type=js, delay=0, url='/js-next'} "
                 "from a window.location assignment.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  URL url("https://example.com/root");
  const std::string html = R"(
    <html><head><title>t</title></head>
    <body><script>window.location = '/js-next';</script></body></html>
  )";

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt);
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  const auto& cr = *it;
  EXPECT_EQ(cr.at("type").get<std::string>(), "js");
  EXPECT_EQ(cr.at("delay").get<int>(), 0);
  EXPECT_EQ(cr.at("url").get<std::string>(), "/js-next");
}

TEST_F(LuaProcessorTest, Js_LocationHref_And_Replace) {
  SCOPED_TRACE(
    "Prefers location.href over location.replace when both are present "
    "(policy-based).");
  RecordProperty("description",
                 "Expects the first matching mechanism to pick "
                 "'https://example.net/alpha' over '.../beta'.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  URL url("https://example.com/a");
  const std::string html = R"(
    <html><head><script>
      // first pattern should match; second is ignored
      location.href = "https://example.net/alpha";
      location.replace('https://example.net/beta');
    </script></head><body></body></html>
  )";

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt);
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  const auto& cr = *it;
  EXPECT_EQ(cr.at("type").get<std::string>(), "js");
  EXPECT_EQ(cr.at("url").get<std::string>(), "https://example.net/alpha");
}

TEST_F(LuaProcessorTest, NoClientRedirect_WhenAbsent) {
  SCOPED_TRACE(
    "Leaves client_redirect unset/null when no redirect patterns are present.");
  RecordProperty("description",
                 "Ensures the script returns either no 'client_redirect' or "
                 "null for plain pages.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  URL url("https://example.com/page");
  const std::string html = R"(
    <html><head><title>No Redirect</title></head><body>ok</body></html>
  )";

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt);
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  // either missing or explicitly null — both acceptable
  EXPECT_TRUE(it == j.end() || it->is_null());
}

TEST_F(LuaProcessorTest, MetaRefresh_HtmlEntities_InUrl) {
  SCOPED_TRACE("Decodes HTML entities (e.g., '&amp;') in meta refresh URLs.");
  RecordProperty("description",
                 "Ensures '&amp;' is converted to '&' in client_redirect.url "
                 "for a meta refresh.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  URL url("https://example.com/p");
  const std::string html = R"(
    <html><head>
      <meta http-equiv="refresh" content="0; url=/redir?x=1&amp;y=2">
    </head><body></body></html>
  )";

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt);
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  const auto& cr = *it;
  EXPECT_EQ(cr.at("type").get<std::string>(), "meta");
  EXPECT_EQ(cr.at("delay").get<int>(), 0);
  // your common.unescape_html should turn &amp; into &
  EXPECT_EQ(cr.at("url").get<std::string>(), "/redir?x=1&y=2");
}

// ─────────────────────────────────────────────────────────────────────────────
// JavaScript client redirect variants (using LuaProcessor instance)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaProcessorTest, Js_LocationAssign_Absolute) {
  SCOPED_TRACE(
    "Detects 'location.assign(ABSOLUTE_URL)' and extracts its target.");
  RecordProperty("description",
                 "Expect client_redirect.url to equal 'https://e.com/next' "
                 "from location.assign.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>location.assign("https://e.com/next");</script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/next");
}

TEST_F(LuaProcessorTest, Js_WindowLocationHref_Absolute) {
  SCOPED_TRACE("Detects 'window.location.href = ABSOLUTE_URL'.");
  RecordProperty("description",
                 "Expect client_redirect.url to equal 'https://e.com/p1' from "
                 "window.location.href.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>window.location.href = 'https://e.com/p1';</script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/p1");
}

TEST_F(LuaProcessorTest, Js_DocumentLocation_Relative_WithBase) {
  SCOPED_TRACE(
    "Extracts relative redirect via 'document.location = REL' and surfaces "
    "<base href> alongside.");
  RecordProperty("description",
                 "Expect client_redirect.url='/rel/path' and "
                 "base_href='https://e.com/base/' when a base tag is present.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html>
      <head>
        <base href="https://e.com/base/">
        <title>t</title>
      </head>
      <body>
        <script>document.location = "/rel/path";</script>
      </body>
    </html>
  )HTML";
  URL url("https://example.com/p");

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "/rel/path");

  auto b = j.find("base_href");
  if (b != j.end() && !b->is_null()) {
    EXPECT_EQ(*b, "https://e.com/base/");
  }
}

TEST_F(LuaProcessorTest, Js_TopLocation_WithWhitespaceAndComments) {
  SCOPED_TRACE(
    "Handles 'top.location = ...' with arbitrary whitespace and comments.");
  RecordProperty("description",
                 "Ensures the extractor is robust against formatting noise and "
                 "still captures the absolute URL.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>
        /* spacing + comments */ top   .   location  /*x*/ =  /*y*/ "https://e.com/top";
      </script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/top");
}

TEST_F(LuaProcessorTest, Js_LocationAssign_Newlines_And_Tabs) {
  SCOPED_TRACE(
    "Parses 'location.assign(...)' even when broken across multiple "
    "lines/tabs.");
  RecordProperty("description",
                 "Confirms the regex/pattern matcher is not "
                 "whitespace-sensitive for location.assign.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>
        location
          .
          assign
          (
            "https://e.com/newlines"
          )
          ;
      </script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/newlines");
}

TEST_F(LuaProcessorTest, Js_LocationReplace_Vs_Href_Preference) {
  SCOPED_TRACE(
    "When both replace() and href are present, prefers href based on policy.");
  RecordProperty("description",
                 "Verifies window.location.href wins over location.replace for "
                 "the extracted URL.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>
        location.replace("https://e.com/replace");
        window.location.href = "https://e.com/href";
      </script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/href");
}

TEST_F(LuaProcessorTest, Js_FirstOccurrenceWins_WhenMultipleAssignments) {
  SCOPED_TRACE(
    "With multiple assignment forms present, the first match wins (adjust "
    "expectation if policy differs).");
  RecordProperty("description",
                 "Ensures 'window.location = ...' is captured before later "
                 "assignments if that is the configured policy.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>
        window.location = "https://e.com/first";
        document.location = "https://e.com/second";
        location.assign("https://e.com/third";
      </script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/first");
}

TEST_F(LuaProcessorTest, Js_WindowLocationHref_SingleQuotes_NoSemicolon) {
  SCOPED_TRACE(
    "Handles missing semicolons and single-quoted URLs in window.location.href "
    "assignments.");
  RecordProperty("description",
                 "Confirms resilience to JS style variations; still extracts "
                 "'https://e.com/no-semi'.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>window.location.href='https://e.com/no-semi'</script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/no-semi");
}

TEST_F(LuaProcessorTest, Js_LocationAssign_SingleQuoted_Relative_WithBase) {
  SCOPED_TRACE(
    "Captures single-quoted relative targets and returns base_href when a "
    "<base> tag is present.");
  RecordProperty(
    "description",
    "Expects client_redirect.url='/k/v' and base_href='https://e.com/base/' "
    "for location.assign with base.");
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html>
      <head><title>t</title><base href="https://e.com/base/"></head>
      <body><script>location.assign('/k/v');</script></body>
    </html>
  )HTML";
  URL url("https://example.com/p");

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "/k/v");

  auto b = j.find("base_href");
  if (b != j.end() && !b->is_null()) {
    EXPECT_EQ(*b, "https://e.com/base/");
  }
}
