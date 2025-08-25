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
