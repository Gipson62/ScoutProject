#include "engine/io/web_server.hpp"

#include <gtest/gtest.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "engine/core/clock.hpp"
#include "engine/core/error.hpp"
#include "engine/core/event.hpp"
#include "engine/io/beast.hpp"
#include "engine/io/sim_transport.hpp"

namespace
{

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

// The engine is single-threaded, and it stays single-threaded here: the server
// gets a thread of its own and the test talks to it over TCP like any browser.
// Nothing shared is touched from both sides without a post onto the server's
// thread.
class ServerFixture : public ::testing::Test
{
 protected:
  auto SetUp() -> void override
  {
    m_root =
        std::filesystem::temp_directory_path() /
        ("sgengine-web-" +
         std::string{
             ::testing::UnitTest::GetInstance()->current_test_info()->name()});
    std::filesystem::remove_all(m_root);
    std::filesystem::create_directories(m_root / "deeper");
    Write("sim.html", "<h1>simulator</h1>");
    Write("app.js", "console.log('hi')");
    Write("deeper/page.html", "<p>deeper</p>");
    Write("../outside.txt", "secret");
  }

  auto TearDown() -> void override
  {
    Stop();
    std::filesystem::remove_all(m_root);
    std::filesystem::remove(m_root.parent_path() / "outside.txt");
  }

  auto Write(std::string_view name, std::string_view content) const -> void
  {
    const std::filesystem::path path = m_root / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
  }

  // Starts the server and runs its io_context on its own thread.
  auto Start() -> void
  {
    m_server = std::make_unique<sg::WebServer>(m_io, m_root);
    const sg::Result<std::uint16_t> port = m_server->Listen(0);
    ASSERT_TRUE(port.has_value()) << port.error().message;
    m_port = *port;
    m_thread = std::thread(
        [this]
        {
          m_io.run();
        });
  }

  auto Stop() -> void
  {
    if (m_thread.joinable())
    {
      m_io.stop();
      m_thread.join();
    }
  }

  // Runs `work` on the server's thread and waits for it, so the test never
  // touches engine state concurrently.
  template <typename F>
  auto OnServerThread(F&& work) -> void
  {
    std::promise<void> done;
    std::future<void> waiting = done.get_future();
    boost::asio::post(m_io,
                      [&done, work = std::forward<F>(work)]() mutable
                      {
                        work();
                        done.set_value();
                      });
    ASSERT_EQ(waiting.wait_for(std::chrono::seconds(5)),
              std::future_status::ready);
  }

  // Frames are read one after another, each read posted by the one before, so
  // a single barrier is not enough: this polls the server's own thread until
  // the condition holds or the wait runs out.
  template <typename P>
  [[nodiscard]] auto WaitFor(P predicate) -> bool
  {
    for (std::int32_t attempt = 0; attempt < 400; ++attempt)
    {
      bool holds = false;
      OnServerThread(
          [&holds, &predicate]
          {
            holds = predicate();
          });
      if (holds)
      {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }

  [[nodiscard]] auto Get(std::string_view target)
      -> http::response<http::string_body>
  {
    boost::asio::io_context client_io;
    tcp::socket socket{client_io};
    socket.connect(
        tcp::endpoint{boost::asio::ip::make_address_v4("127.0.0.1"), m_port});

    http::request<http::string_body> request{http::verb::get, target, 11};
    request.set(http::field::host, "127.0.0.1");
    http::write(socket, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    return response;
  }

  // A connected browser, for the WebSocket tests.
  class Client
  {
   public:
    explicit Client(std::uint16_t port) : m_ws(m_io)
    {
      m_ws.next_layer().connect(
          tcp::endpoint{boost::asio::ip::make_address_v4("127.0.0.1"), port});
      m_ws.handshake("127.0.0.1", "/ws");
    }

    auto Send(std::string_view text) -> void
    {
      m_ws.text(true);
      m_ws.write(boost::asio::buffer(text));
    }

    [[nodiscard]] auto Receive() -> boost::json::object
    {
      beast::flat_buffer buffer;
      m_ws.read(buffer);
      boost::system::error_code ec;
      const boost::json::value value =
          boost::json::parse(beast::buffers_to_string(buffer.data()), ec);
      EXPECT_FALSE(ec) << ec.message();
      return value.is_object() ? value.get_object() : boost::json::object{};
    }

   private:
    boost::asio::io_context m_io;
    websocket::stream<tcp::socket> m_ws;
  };

  std::filesystem::path m_root;
  boost::asio::io_context m_io;
  std::unique_ptr<sg::WebServer> m_server;
  std::uint16_t m_port = 0;
  std::thread m_thread;
};

TEST_F(ServerFixture, RefusesToListenWithoutAWebDirectory)
{
  sg::WebServer server{m_io, m_root / "nowhere"};
  const sg::Result<std::uint16_t> port = server.Listen(0);
  ASSERT_FALSE(port.has_value());
  EXPECT_EQ(port.error().code, sg::ErrorCode::kIoWebRootMissing);
}

TEST_F(ServerFixture, ServesStaticFilesWithTheirMimeTypes)
{
  Start();

  const auto html = Get("/sim.html");
  EXPECT_EQ(html.result(), http::status::ok);
  EXPECT_EQ(html.body(), "<h1>simulator</h1>");
  EXPECT_EQ(html[http::field::content_type], "text/html; charset=utf-8");

  const auto script = Get("/app.js");
  EXPECT_EQ(script.result(), http::status::ok);
  EXPECT_EQ(script[http::field::content_type],
            "application/javascript; charset=utf-8");

  EXPECT_EQ(Get("/deeper/page.html").body(), "<p>deeper</p>");
}

TEST_F(ServerFixture, ServesTheSimulatorAtTheRoot)
{
  Start();
  const auto response = Get("/");
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "<h1>simulator</h1>");
}

TEST_F(ServerFixture, RefusesToLeaveTheWebDirectory)
{
  Start();
  EXPECT_EQ(Get("/../outside.txt").result(), http::status::not_found);
  EXPECT_EQ(Get("/deeper/../../outside.txt").result(), http::status::not_found);
  EXPECT_EQ(Get("/nothing.html").result(), http::status::not_found);
}

TEST_F(ServerFixture, CarriesAFrameFromTheBrowserToItsHandler)
{
  Start();
  std::vector<boost::json::object> received;
  OnServerThread(
      [this, &received]
      {
        m_server->On("sim.tap",
                     [&received](const boost::json::object& frame)
                     {
                       received.push_back(frame);
                     });
      });

  Client client{m_port};
  client.Send(R"({"type":"sim.tap","point":"P3","uid":"04A1B2C3"})");

  ASSERT_TRUE(WaitFor(
      [&received]
      {
        return received.size() == 1;
      }));
  OnServerThread(
      [&received]
      {
        EXPECT_EQ(received[0].at("point").as_string(), "P3");
      });
}

TEST_F(ServerFixture, CountsFramesNobodyHandlesInsteadOfActingOnThem)
{
  Start();
  Client client{m_port};
  client.Send(R"({"type":"sim.tap","point":"P3","uid":"04"})");  // no handler
                                                                 // registered
  client.Send(R"({"no_type":true})");
  client.Send("not json at all");

  EXPECT_TRUE(WaitFor(
      [this]
      {
        return m_server->UnhandledFrames() == 3U;
      }));
}

TEST_F(ServerFixture, BroadcastsToEveryConnectedBrowser)
{
  Start();
  Client first{m_port};
  Client second{m_port};
  OnServerThread(
      [this]
      {
        EXPECT_EQ(m_server->ClientCount(), 2U);
      });

  OnServerThread(
      [this]
      {
        boost::json::object frame;
        frame["type"] = "led";
        frame["point"] = "P3";
        m_server->Broadcast(frame);
      });

  EXPECT_EQ(first.Receive().at("point").as_string(), "P3");
  EXPECT_EQ(second.Receive().at("point").as_string(), "P3");
}

TEST_F(ServerFixture, TellsTheEngineWhenABrowserConnects)
{
  Start();
  std::int32_t connects = 0;
  OnServerThread(
      [this, &connects]
      {
        m_server->OnConnect(
            [&connects]
            {
              ++connects;
            });
      });

  Client first{m_port};
  OnServerThread([] {});
  Client second{m_port};
  OnServerThread(
      [&connects]
      {
        EXPECT_EQ(connects, 2);
      });
}

// --- SimTransport over the same socket ---

TEST_F(ServerFixture, SimTransportTurnsFramesIntoEventsAndCommandsIntoFrames)
{
  Start();
  sg::FakeClock clock{4321};
  sg::SimTransport transport{*m_server, clock};
  std::vector<sg::Event> events;

  OnServerThread(
      [&transport, &events]
      {
        transport.Start(
            [&events](sg::Event event)
            {
              events.push_back(event);
            });
      });

  Client client{m_port};
  client.Send(R"({"type":"sim.tap","point":"P3","uid":"04A1B2C3"})");
  client.Send(R"({"type":"sim.button","point":"P5"})");
  client.Send(R"({"type":"sim.tap","point":"P3"})");  // no uid: rejected

  ASSERT_TRUE(WaitFor(
      [&transport, &events]
      {
        return events.size() == 2 && transport.RejectedFrames() == 1;
      }));
  OnServerThread(
      [&events, &transport]
      {
        ASSERT_EQ(events.size(), 2U);
        EXPECT_EQ(events[0].t, 4321);
        EXPECT_EQ(std::get<sg::Tap>(events[0].data).point, "P3");
        EXPECT_EQ(std::get<sg::Tap>(events[0].data).uid, "04A1B2C3");
        EXPECT_EQ(std::get<sg::Button>(events[1].data).point, "P5");
        EXPECT_EQ(transport.RejectedFrames(), 1U);
      });

  OnServerThread(
      [&transport]
      {
        transport.Send(
            sg::LedState{"P3", {255, 120, 0}, sg::Pattern::kPulse, 12});
        transport.Send(sg::Beep{"P3", sg::BeepKind::kOk});
      });

  const boost::json::object led = client.Receive();
  EXPECT_EQ(led.at("type").as_string(), "led");
  EXPECT_EQ(led.at("pattern").as_string(), "pulse");
  EXPECT_EQ(led.at("v").as_int64(), 12);

  const boost::json::object beep = client.Receive();
  EXPECT_EQ(beep.at("type").as_string(), "beep");
  EXPECT_EQ(beep.at("kind").as_string(), "ok");
}

}  // namespace
