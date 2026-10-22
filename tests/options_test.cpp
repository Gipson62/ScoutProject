#include "engine/options.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string_view>
#include <vector>

#include "engine/core/error.hpp"

namespace
{

[[nodiscard]] auto Parse(std::vector<std::string_view> arguments)
    -> sg::Result<sg::Options>
{
  return sg::ParseOptions(arguments);
}

TEST(Options, DefaultsMatchTheDocumentedCommand)
{
  const sg::Result<sg::Options> options = Parse({});
  ASSERT_TRUE(options.has_value()) << options.error().message;
  EXPECT_EQ(options->config, "config/dev.toml");
  EXPECT_EQ(options->session, "sessions/dev.jsonl");
  EXPECT_EQ(options->web, "web");
  EXPECT_EQ(options->port, 8080);
  EXPECT_FALSE(options->help);
}

TEST(Options, ReadsEveryOption)
{
  const sg::Result<sg::Options> options =
      Parse({"--config", "config/camp2027.toml", "--session",
             "sessions/day1.jsonl", "--web", "/srv/web", "--port", "9000"});
  ASSERT_TRUE(options.has_value()) << options.error().message;
  EXPECT_EQ(options->config, "config/camp2027.toml");
  EXPECT_EQ(options->session, "sessions/day1.jsonl");
  EXPECT_EQ(options->web, "/srv/web");
  EXPECT_EQ(options->port, 9000);
}

TEST(Options, HelpStopsReadingTheRest)
{
  const sg::Result<sg::Options> options =
      Parse({"--help", "--port", "nonsense"});
  ASSERT_TRUE(options.has_value()) << options.error().message;
  EXPECT_TRUE(options->help);
}

TEST(Options, RejectsATypoInsteadOfIgnoringIt)
{
  const sg::Result<sg::Options> options = Parse({"--prot", "9000"});
  ASSERT_FALSE(options.has_value());
  EXPECT_EQ(options.error().message, "unknown option: --prot");
}

TEST(Options, RejectsAMissingValue)
{
  const sg::Result<sg::Options> options = Parse({"--config"});
  ASSERT_FALSE(options.has_value());
  EXPECT_EQ(options.error().message, "--config needs a value");
}

TEST(Options, RejectsAPortThatIsNotOne)
{
  for (const std::string_view bad : {"0", "65536", "80x", "", "-1", "8 0"})
  {
    const sg::Result<sg::Options> options = Parse({"--port", bad});
    ASSERT_FALSE(options.has_value()) << "accepted port " << bad;
    EXPECT_EQ(options.error().code, sg::ErrorCode::kConfigBadValue);
  }
}

TEST(Options, UsageNamesEveryOption)
{
  const std::string_view usage = sg::UsageText();
  for (const std::string_view option :
       {"--config", "--session", "--web", "--port", "--help"})
  {
    EXPECT_NE(usage.find(option), std::string_view::npos) << option;
  }
}

}  // namespace
