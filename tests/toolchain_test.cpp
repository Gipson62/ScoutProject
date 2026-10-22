#include "engine/core/toolchain.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <string>
#include <utility>

namespace
{

enum class Colour : std::uint8_t
{
  kRed = 7
};

// std::expected and std::to_underlying are the C++23 features the whole engine
// is built on; a toolchain that lacks either cannot build anything later.
TEST(Toolchain, ProvidesTheCpp23FeaturesTheEngineNeeds)
{
  const std::expected<std::int32_t, std::string> ok = 41;
  EXPECT_EQ(ok.transform(
                  [](std::int32_t v)
                  {
                    return v + 1;
                  })
                .value(),
            42);

  const std::expected<std::int32_t, std::string> bad =
      std::unexpected(std::string{"nope"});
  EXPECT_EQ(bad.or_else(
                   [](const std::string&)
                   {
                     return std::expected<std::int32_t, std::string>{0};
                   })
                .value(),
            0);

  EXPECT_EQ(std::to_underlying(Colour::kRed), 7);
}

TEST(Toolchain, NamesTheCompiler)
{
  EXPECT_FALSE(sg::ToolchainName().empty());
}

// pocketpy is linked into a binary compiled with -fno-exceptions, through its C
// API only.
TEST(Toolchain, RunsAnEmbeddedScript)
{
  EXPECT_EQ(sg::RunEmbeddedScript("'tap ' + str(3 * 7)"), "tap 21");
}

// toml++ with TOML_EXCEPTIONS off reports a bad document instead of throwing.
TEST(Toolchain, ParsesTomlWithoutExceptions)
{
  EXPECT_EQ(sg::ParseTomlInteger("seed = 2027\n", "seed"), 2027);
  EXPECT_EQ(sg::ParseTomlInteger("seed = = 2027\n", "seed"), 0);
  EXPECT_EQ(sg::ParseTomlInteger("seed = 2027\n", "mode"), 0);
}

}  // namespace
