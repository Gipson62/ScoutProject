#include "engine/core/error.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace
{

[[nodiscard]] auto Halve(std::int32_t value) -> sg::Result<std::int32_t>
{
  if (value % 2 != 0)
  {
    return sg::Fail(sg::ErrorCode::kBadValue, "odd: " + std::to_string(value));
  }
  return value / 2;
}

TEST(Error, FailCarriesCodeAndMessage)
{
  const sg::Result<std::int32_t> result = Halve(7);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, sg::ErrorCode::kBadValue);
  EXPECT_EQ(result.error().message, "odd: 7");
}

TEST(Error, ChainsWithAndThenAndTransform)
{
  EXPECT_EQ(Halve(8)
                .and_then(Halve)
                .transform(
                    [](std::int32_t v)
                    {
                      return v + 1;
                    })
                .value(),
            3);

  const sg::Result<std::int32_t> stopped =
      // 8 -> 4 -> 2 -> 1, and 1 is odd: the fourth step fails and the chain
      // stops.
      Halve(8).and_then(Halve).and_then(Halve).and_then(Halve);
  ASSERT_FALSE(stopped.has_value());
  EXPECT_EQ(stopped.error().message, "odd: 1");
}

TEST(Error, DescribesEveryCodeItKnows)
{
  EXPECT_EQ(sg::Describe(sg::ErrorCode::kNone), "no error");
  EXPECT_EQ(sg::Describe(sg::ErrorCode::kMissingField),
            "a required field is missing");
  EXPECT_EQ(sg::Describe(static_cast<sg::ErrorCode>(60000)),
            "unrecognised error code");
}

TEST(Error, ResultOfVoidReportsSuccessAndFailure)
{
  const sg::Result<void> ok{};
  EXPECT_TRUE(ok.has_value());

  const sg::Result<void> bad =
      sg::Fail(sg::ErrorCode::kWrongType, "not a string");
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error().code, sg::ErrorCode::kWrongType);
}

}  // namespace
