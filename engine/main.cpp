// sgengine: wires the adapters to the Core and runs one io_context.
//
//   sgengine --config config/dev.toml --session sessions/dev.jsonl
//            --web web --port 8080
//
// Everything that touches game state runs on this one thread, so there is no
// mutex anywhere in the engine.

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/config/config.hpp"
#include "engine/config/roster.hpp"
#include "engine/core/clock.hpp"
#include "engine/core/command_router.hpp"
#include "engine/core/core.hpp"
#include "engine/core/error.hpp"
#include "engine/core/event.hpp"
#include "engine/core/mode.hpp"
#include "engine/core/toolchain.hpp"
#include "engine/io/output_sink.hpp"
#include "engine/io/sim_transport.hpp"
#include "engine/io/state_frame.hpp"
#include "engine/io/web_server.hpp"
#include "engine/log/event_log.hpp"
#include "engine/options.hpp"
#include "engine/script/script_host.hpp"

namespace
{

// How often timers that fell due between inputs are fired, as DESIGN.md
// specifies.
constexpr std::chrono::milliseconds kTickEvery{100};

[[nodiscard]] auto ReadWholeFile(const std::filesystem::path& path)
    -> sg::Result<std::string>
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return sg::Fail(sg::ErrorCode::kConfigOpenFailed,
                    "cannot read " + path.string());
  }
  return std::string{std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>()};
}

// Where game time resumes: the last logged event. A fresh session starts at 0.
// Restarting at 0 with a log already in it would make every new event look late
// and force a rebuild on each one.
[[nodiscard]] auto ResumeAt(const sg::EventLog& log) -> sg::Millis
{
  sg::Millis latest = 0;
  for (const sg::Event& event : log.Events())
  {
    latest = std::max(latest, event.t);
  }
  return latest;
}

// Everything the engine owns, in construction order, so main stays readable and
// destruction unwinds in the right order.
class Application
{
 public:
  Application(sg::Config config, sg::CsvRoster roster, sg::EventLog log,
              const sg::Options& options, std::string mode_source)
      : m_config(std::move(config)),
        m_roster(std::move(roster)),
        m_log(std::move(log)),
        m_mode_source(std::move(mode_source)),
        m_mode_name(m_config.mode_path.filename().string()),
        m_clock(ResumeAt(m_log)),
        m_web(m_io, options.web),
        m_transport(m_web, m_clock),
        m_sink(m_transport, m_web),
        m_router(m_sink),
        m_core(m_clock, m_log, m_router, m_roster, m_config.setup,
               [this]
               {
                 return std::make_unique<sg::ScriptHost>(m_mode_source,
                                                         m_mode_name);
               }),
        m_signals(m_io, SIGINT, SIGTERM),
        m_ticker(m_io)
  {
  }

  [[nodiscard]] auto Run(const sg::Options& options) -> sg::Result<void>
  {
    const sg::Result<std::uint16_t> port = m_web.Listen(options.port);
    if (!port)
    {
      return std::unexpected(port.error());
    }

    // Every input: into the log and through the rules, then told to the pages.
    m_transport.Start(
        [this](sg::Event event)
        {
          Apply(std::move(event));
        });

    // A page that just opened knows nothing, so it is told everything.
    m_web.OnConnect(
        [this]
        {
          PushState();
        });

    m_signals.async_wait(
        [this](const boost::system::error_code&, int)
        {
          std::println("stopping");
          m_io.stop();
        });

    Tick();

    // Replays whatever the session file already holds: this is what makes a
    // restart resume the game rather than start a new one.
    m_core.Start();
    m_last_hash = m_core.State().Hash();

    std::println("sgengine, built with {}", sg::ToolchainName());
    std::println("  game      {}", m_config.setup.title);
    std::println("  mode      {}", m_config.mode_path.string());
    std::println("  teams     {}, points {}", m_config.setup.teams.size(),
                 m_config.setup.points.size());
    std::println("  roster    {} wristbands from {}", m_roster.Size(),
                 m_config.roster_path.string());
    std::println("  session   {} events replayed", m_log.Events().size());
    for (const std::string& warning : m_log.Warnings())
    {
      std::println(stderr, "  warning   {}", warning);
    }
    std::println("  simulator http://localhost:{}/", *port);
    std::println("  dashboard http://localhost:{}/dashboard.html", *port);

    m_io.run();
    return {};
  }

 private:
  auto Apply(sg::Event event) -> void
  {
    const sg::Result<void> applied = m_core.OnInput(std::move(event));
    if (!applied)
    {
      // A log that cannot be written is the one failure the engine cannot play
      // through: the next replay would be wrong, so it says so and stops.
      std::println(stderr, "cannot write the session log: {}",
                   applied.error().message);
      m_io.stop();
      return;
    }
    if (!m_log.Events().empty())
    {
      m_web.Broadcast(sg::EventFrame(m_log.Events().back()));
    }
    PushStateIfChanged();
  }

  auto Tick() -> void
  {
    m_ticker.expires_after(kTickEvery);
    m_ticker.async_wait(
        [this](const boost::system::error_code& ec)
        {
          if (ec)
          {
            return;
          }
          m_core.Tick();
          PushStateIfChanged();
          Tick();
        });
  }

  auto PushState() -> void
  {
    const std::vector<sg::Tag> wristbands = m_roster.Entries();
    m_web.Broadcast(sg::StateFrame(m_core.State(), m_clock.Now(), wristbands));
    m_last_hash = m_core.State().Hash();
  }

  // The pages are kept honest by comparing the state they were last told about
  // with the state now: a timer that fired changes the game with no input to
  // hang a frame on.
  auto PushStateIfChanged() -> void
  {
    const std::uint64_t hash = m_core.State().Hash();
    if (hash != m_last_hash)
    {
      PushState();
    }
  }

  sg::Config m_config;
  sg::CsvRoster m_roster;
  sg::EventLog m_log;
  std::string m_mode_source;
  std::string m_mode_name;

  boost::asio::io_context m_io;
  sg::SteadyClock m_clock;
  sg::WebServer m_web;
  sg::SimTransport m_transport;
  sg::OutputSink m_sink;
  sg::CommandRouter m_router;
  sg::Core m_core;
  boost::asio::signal_set m_signals;
  boost::asio::steady_timer m_ticker;
  std::uint64_t m_last_hash = 0;
};

}  // namespace

auto main(int argc, char** argv) -> int
{
  std::vector<std::string_view> arguments;
  arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
  for (std::int32_t i = 1; i < argc; ++i)
  {
    arguments.emplace_back(argv[i]);
  }

  const sg::Result<sg::Options> options = sg::ParseOptions(arguments);
  if (!options)
  {
    std::println(stderr, "{}\n", options.error().message);
    std::print(stderr, "{}", sg::UsageText());
    return 2;
  }
  if (options->help)
  {
    std::print("{}", sg::UsageText());
    return 0;
  }

  sg::Result<sg::Config> config = sg::LoadConfig(options->config);
  if (!config)
  {
    std::println(stderr, "{}", config.error().message);
    return 1;
  }

  sg::Result<sg::CsvRoster> roster = sg::CsvRoster::Load(config->roster_path);
  if (!roster)
  {
    std::println(stderr, "{}", roster.error().message);
    return 1;
  }

  const sg::Result<std::string> mode_source = ReadWholeFile(config->mode_path);
  if (!mode_source)
  {
    std::println(stderr, "{}", mode_source.error().message);
    return 1;
  }

  // The mode is loaded once here, before anything else is built, so a script
  // that will not compile stops the engine instead of letting it play a game
  // whose rules never ran. Only one ScriptHost may live at a time, so this one
  // is gone before the Core makes its own.
  {
    const sg::ScriptHost check{*mode_source,
                               config->mode_path.filename().string()};
    if (const std::optional<std::string> error = check.LoadError())
    {
      std::println(stderr, "{}", *error);
      return 1;
    }
  }

  sg::Result<sg::EventLog> log = sg::EventLog::Open(options->session);
  if (!log)
  {
    std::println(stderr, "{}", log.error().message);
    return 1;
  }

  Application application{std::move(*config), std::move(*roster),
                          std::move(*log), *options, *mode_source};
  const sg::Result<void> ran = application.Run(*options);
  if (!ran)
  {
    std::println(stderr, "{}", ran.error().message);
    return 1;
  }
  return 0;
}
