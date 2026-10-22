#include "engine/log/event_log.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "engine/core/error.hpp"
#include "engine/core/event.hpp"

namespace
{

class EventLogTest : public ::testing::Test
{
 protected:
  auto SetUp() -> void override
  {
    m_dir =
        std::filesystem::temp_directory_path() /
        ("sgengine-log-" +
         std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
         "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(m_dir);
  }

  auto TearDown() -> void override
  {
    std::filesystem::remove_all(m_dir);
  }

  [[nodiscard]] auto Path(std::string_view name = "session.jsonl") const
      -> std::filesystem::path
  {
    return m_dir / name;
  }

  auto Write(const std::filesystem::path& path, std::string_view content) const
      -> void
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
  }

  [[nodiscard]] auto ReadBack(const std::filesystem::path& path) const
      -> std::string
  {
    std::ifstream in(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
  }

 private:
  std::filesystem::path m_dir;
};

TEST_F(EventLogTest, OpenCreatesTheFileAndItsDirectory)
{
  const std::filesystem::path path = Path("deeper/session.jsonl");
  ASSERT_FALSE(std::filesystem::exists(path));

  const sg::Result<sg::EventLog> log = sg::EventLog::Open(path);
  ASSERT_TRUE(log.has_value()) << log.error().message;
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_TRUE(log->Events().empty());
  EXPECT_EQ(log->NextSeq(), 1U);
}

TEST_F(EventLogTest, AppendNumbersFromOneAndWritesOneLinePerEvent)
{
  sg::Result<sg::EventLog> log = sg::EventLog::Open(Path());
  ASSERT_TRUE(log.has_value()) << log.error().message;

  EXPECT_EQ(log->Append(sg::Event{0, 100, sg::Tap{"P1", "04A1"}}).value(), 1U);
  EXPECT_EQ(log->Append(sg::Event{0, 200, sg::Button{"P2"}}).value(), 2U);
  EXPECT_EQ(log->Append(sg::Event{0, 300, sg::Void{1}}).value(), 3U);

  EXPECT_EQ(ReadBack(Path()),
            "{\"seq\":1,\"t\":100,\"kind\":\"tap\",\"point\":\"P1\",\"uid\":"
            "\"04A1\"}\n"
            "{\"seq\":2,\"t\":200,\"kind\":\"button\",\"point\":\"P2\"}\n"
            "{\"seq\":3,\"t\":300,\"kind\":\"void\",\"target\":1}\n");

  // A sequence number the caller supplied is overwritten: only the log assigns
  // them, so two writers can never agree on a wrong one.
  EXPECT_EQ(log->Append(sg::Event{99, 400, sg::Button{"P3"}}).value(), 4U);
  EXPECT_EQ(log->Events().size(), 4U);
  EXPECT_EQ(log->Events().back().seq, 4U);
}

TEST_F(EventLogTest, AppendMakesTheLineReadableImmediately)
{
  sg::Result<sg::EventLog> log = sg::EventLog::Open(Path());
  ASSERT_TRUE(log.has_value()) << log.error().message;
  static_cast<void>(
      log->Append(sg::Event{0, 10, sg::Tap{"P1", "04A1"}}).value());

  // Append returns only after the flush and the fsync, so a reader that opens
  // the file in the same breath already sees the line: this is what makes a
  // power cut lose at most the event that was never acknowledged.
  const sg::Result<sg::LoadedLog> loaded = sg::EventLog::Load(Path());
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  EXPECT_EQ(loaded->events.size(), 1U);
}

TEST_F(EventLogTest, OpenContinuesTheNumberingOfAnExistingFile)
{
  {
    sg::Result<sg::EventLog> first = sg::EventLog::Open(Path());
    ASSERT_TRUE(first.has_value()) << first.error().message;
    static_cast<void>(
        first->Append(sg::Event{0, 10, sg::Button{"P1"}}).value());
    static_cast<void>(
        first->Append(sg::Event{0, 20, sg::Button{"P2"}}).value());
  }

  sg::Result<sg::EventLog> second = sg::EventLog::Open(Path());
  ASSERT_TRUE(second.has_value()) << second.error().message;
  EXPECT_EQ(second->Events().size(), 2U);
  EXPECT_EQ(second->NextSeq(), 3U);
  EXPECT_EQ(second->Append(sg::Event{0, 30, sg::Button{"P3"}}).value(), 3U);
}

TEST_F(EventLogTest, LoadReadsEveryEventBack)
{
  Write(Path(),
        "{\"seq\":1,\"t\":100,\"kind\":\"tap\",\"point\":\"P1\",\"uid\":"
        "\"04A1\"}\n"
        "{\"seq\":2,\"t\":200,\"kind\":\"action\",\"team\":\"Wolves\",\"name\":"
        "\"recruit\"}\n");

  const sg::Result<sg::LoadedLog> loaded = sg::EventLog::Load(Path());
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  ASSERT_EQ(loaded->events.size(), 2U);
  EXPECT_TRUE(loaded->warnings.empty());
  EXPECT_EQ(std::get<sg::Tap>(loaded->events[0].data).uid, "04A1");
  EXPECT_EQ(std::get<sg::Action>(loaded->events[1].data).name, "recruit");
}

TEST_F(EventLogTest, LoadFailsOnACorruptLineAndNamesItsNumber)
{
  Write(Path(),
        "{\"seq\":1,\"t\":100,\"kind\":\"button\",\"point\":\"P1\"}\n"
        "{\"seq\":2,\"t\":200,\"kind\":\"butt\n"
        "{\"seq\":3,\"t\":300,\"kind\":\"button\",\"point\":\"P3\"}\n");

  const sg::Result<sg::LoadedLog> loaded = sg::EventLog::Load(Path());
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, sg::ErrorCode::kLogCorruptLine);
  EXPECT_NE(loaded.error().message.find(":2:"), std::string::npos)
      << loaded.error().message;
}

TEST_F(EventLogTest, LoadFailsOnALineThatIsValidJsonButNotAnEvent)
{
  Write(Path(), "{\"seq\":1,\"t\":100,\"kind\":\"tap\",\"point\":\"P1\"}\n");

  const sg::Result<sg::LoadedLog> loaded = sg::EventLog::Load(Path());
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, sg::ErrorCode::kLogCorruptLine);
  EXPECT_NE(loaded.error().message.find("uid"), std::string::npos)
      << loaded.error().message;
}

TEST_F(EventLogTest, LoadSkipsATruncatedLastLineWithAWarning)
{
  // What a power cut during a write leaves behind.
  Write(Path(),
        "{\"seq\":1,\"t\":100,\"kind\":\"button\",\"point\":\"P1\"}\n"
        "{\"seq\":2,\"t\":200,\"kind\":\"butt");

  const sg::Result<sg::LoadedLog> loaded = sg::EventLog::Load(Path());
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  EXPECT_EQ(loaded->events.size(), 1U);
  ASSERT_EQ(loaded->warnings.size(), 1U);
  EXPECT_NE(loaded->warnings[0].find("line 2"), std::string::npos)
      << loaded->warnings[0];
  EXPECT_NE(loaded->warnings[0].find("truncated"), std::string::npos)
      << loaded->warnings[0];
}

TEST_F(EventLogTest, OpenCarriesTheWarningAndAppendsAfterTheGoodLines)
{
  Write(Path(),
        "{\"seq\":1,\"t\":100,\"kind\":\"button\",\"point\":\"P1\"}\n"
        "{\"seq\":2,\"t\":200,\"kind\":\"but");

  sg::Result<sg::EventLog> log = sg::EventLog::Open(Path());
  ASSERT_TRUE(log.has_value()) << log.error().message;
  EXPECT_EQ(log->Warnings().size(), 1U);
  EXPECT_EQ(log->NextSeq(), 2U);

  // The partial line is still in the file, so the next append has to leave a
  // readable log: the new line starts on its own line.
  static_cast<void>(log->Append(sg::Event{0, 300, sg::Button{"P3"}}).value());
  const sg::Result<sg::LoadedLog> reloaded = sg::EventLog::Load(Path());
  ASSERT_FALSE(reloaded.has_value())
      << "a partial line glued to the next one must be reported";
  EXPECT_EQ(reloaded.error().code, sg::ErrorCode::kLogCorruptLine);
}

TEST_F(EventLogTest, LoadReportsAMissingFile)
{
  const sg::Result<sg::LoadedLog> loaded =
      sg::EventLog::Load(Path("nowhere.jsonl"));
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, sg::ErrorCode::kLogOpenFailed);
}

TEST_F(EventLogTest, ByTimeWithoutVoidedOrdersByTrueTimeThenSequence)
{
  sg::Result<sg::EventLog> log = sg::EventLog::Open(Path());
  ASSERT_TRUE(log.has_value()) << log.error().message;

  static_cast<void>(log->Append(sg::Event{0, 300, sg::Button{"late"}}).value());
  static_cast<void>(
      log->Append(sg::Event{0, 100, sg::Button{"early"}}).value());
  static_cast<void>(log->Append(sg::Event{0, 300, sg::Button{"tie"}}).value());

  const std::vector<sg::Event> replay = log->ByTimeWithoutVoided();
  ASSERT_EQ(replay.size(), 3U);
  EXPECT_EQ(std::get<sg::Button>(replay[0].data).point, "early");
  EXPECT_EQ(std::get<sg::Button>(replay[1].data).point,
            "late");  // seq 1 before seq 3
  EXPECT_EQ(std::get<sg::Button>(replay[2].data).point, "tie");
}

TEST_F(EventLogTest, ByTimeWithoutVoidedDropsTheTargetAndTheVoidItself)
{
  sg::Result<sg::EventLog> log = sg::EventLog::Open(Path());
  ASSERT_TRUE(log.has_value()) << log.error().message;

  const std::uint64_t mistake =
      log->Append(sg::Event{0, 100, sg::Button{"mistake"}}).value();
  static_cast<void>(log->Append(sg::Event{0, 200, sg::Button{"kept"}}).value());
  static_cast<void>(log->Append(sg::Event{0, 900, sg::Void{mistake}}).value());

  const std::vector<sg::Event> replay = log->ByTimeWithoutVoided();
  ASSERT_EQ(replay.size(), 1U);
  EXPECT_EQ(std::get<sg::Button>(replay[0].data).point, "kept");

  // The log itself keeps all three: voiding is a new line, never an edit.
  EXPECT_EQ(log->Events().size(), 3U);
}

}  // namespace
