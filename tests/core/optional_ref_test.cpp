#include "engine/core/optional_ref.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{

struct Team
{
  std::string name;
  std::int32_t points = 0;
};

[[nodiscard]] auto Find(std::vector<Team>& teams, std::string_view name)
    -> sg::OptionalRef<Team>
{
  for (Team& team : teams)
  {
    if (team.name == name)
    {
      return team;
    }
  }
  return std::nullopt;
}

TEST(OptionalRef, FoundReferenceReadsAndWritesTheOriginal)
{
  std::vector<Team> teams{{"Wolves", 3}, {"Foxes", 5}};

  sg::OptionalRef<Team> found = Find(teams, "Foxes");
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found->name, "Foxes");
  EXPECT_EQ((*found).points, 5);

  found->points = 9;
  EXPECT_EQ(teams[1].points, 9);
}

TEST(OptionalRef, AbsentReferenceIsEmpty)
{
  std::vector<Team> teams{{"Wolves", 3}};

  const sg::OptionalRef<Team> missing = Find(teams, "Bears");
  EXPECT_FALSE(missing.has_value());
  EXPECT_FALSE(static_cast<bool>(missing));
}

TEST(OptionalRef, AssignmentRebindsInsteadOfAssigningThrough)
{
  Team wolves{"Wolves", 3};
  Team foxes{"Foxes", 5};

  sg::OptionalRef<Team> ref = wolves;
  ref = foxes;

  EXPECT_EQ(ref->name, "Foxes");
  EXPECT_EQ(wolves.name, "Wolves");  // wolves was not overwritten by foxes

  ref = std::nullopt;
  EXPECT_FALSE(ref.has_value());
}

TEST(OptionalRef, ValueOrReturnsTheFallbackWhenAbsent)
{
  std::vector<Team> teams{{"Wolves", 3}};

  EXPECT_EQ(Find(teams, "Wolves").value_or(Team{"none", -1}).name, "Wolves");
  EXPECT_EQ(Find(teams, "Bears").value_or(Team{"none", -1}).name, "none");
}

TEST(OptionalRef, TransformAndAndThenSkipAnAbsentReference)
{
  std::vector<Team> teams{{"Wolves", 3}};

  EXPECT_EQ(Find(teams, "Wolves")
                .transform(
                    [](const Team& t)
                    {
                      return t.points * 2;
                    })
                .value(),
            6);
  EXPECT_FALSE(Find(teams, "Bears")
                   .transform(
                       [](const Team& t)
                       {
                         return t.points;
                       })
                   .has_value());

  const auto first_letter = [](const Team& t) -> std::optional<char>
  {
    if (t.name.empty())
    {
      return std::nullopt;
    }
    return t.name.front();
  };
  EXPECT_EQ(Find(teams, "Wolves").and_then(first_letter).value(), 'W');
  EXPECT_FALSE(Find(teams, "Bears").and_then(first_letter).has_value());
}

TEST(OptionalRef, ConvertsToAConstReference)
{
  Team wolves{"Wolves", 3};
  const sg::OptionalRef<Team> mutable_ref = wolves;
  const sg::OptionalRef<const Team> const_ref = mutable_ref;

  ASSERT_TRUE(const_ref.has_value());
  EXPECT_EQ(const_ref->points, 3);
}

}  // namespace
