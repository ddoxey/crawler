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
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>location.assign("https://e.com/next");</script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");
  SCOPED_TRACE(html);

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/next");
  // EXPECT_EQ((*it)["mechanism"], "js_location_assign");
}

TEST_F(LuaProcessorTest, Js_WindowLocationHref_Absolute) {
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>window.location.href = 'https://e.com/p1';</script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");
  SCOPED_TRACE(html);

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/p1");
  // EXPECT_EQ((*it)["mechanism"], "js_location_href");
}

TEST_F(LuaProcessorTest, Js_DocumentLocation_Relative_WithBase) {
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
  SCOPED_TRACE(html);

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
  // EXPECT_EQ((*it)["mechanism"], "js_document_location");
}

TEST_F(LuaProcessorTest, Js_TopLocation_WithWhitespaceAndComments) {
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>
        /* spacing + comments */ top   .   location  /*x*/ =  /*y*/ "https://e.com/top";
      </script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");
  SCOPED_TRACE(html);

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/top");
  // EXPECT_EQ((*it)["mechanism"], "js_top_location");
}

TEST_F(LuaProcessorTest, Js_LocationAssign_Newlines_And_Tabs) {
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
  SCOPED_TRACE(html);

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/newlines");
}

TEST_F(LuaProcessorTest, Js_LocationReplace_Vs_Href_Preference) {
  // If your policy prefers href over replace(), expect the href URL.
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
  SCOPED_TRACE(html);

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/href");
}

TEST_F(LuaProcessorTest, Js_FirstOccurrenceWins_WhenMultipleAssignments) {
  // Adjust expectation if your policy is "last wins".
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
  SCOPED_TRACE(html);

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/first");
}

TEST_F(LuaProcessorTest, Js_WindowLocationHref_SingleQuotes_NoSemicolon) {
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html><head><title>t</title></head><body>
      <script>window.location.href='https://e.com/no-semi'</script>
    </body></html>
  )HTML";
  URL url("https://example.com/p");
  SCOPED_TRACE(html);

  auto jopt = lp.Process(url, html);
  ASSERT_TRUE(jopt.has_value());
  const auto& j = *jopt;

  auto it = j.find("client_redirect");
  ASSERT_NE(it, j.end());
  EXPECT_EQ((*it)["url"], "https://e.com/no-semi");
}

TEST_F(LuaProcessorTest, Js_LocationAssign_SingleQuoted_Relative_WithBase) {
  LuaProcessor lp(scripts_dir_, URL("example.com"));
  const std::string html = R"HTML(
    <html>
      <head><title>t</title><base href="https://e.com/base/"></head>
      <body><script>location.assign('/k/v');</script></body>
    </html>
  )HTML";
  URL url("https://example.com/p");
  SCOPED_TRACE(html);

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
