#ifndef SG_ENGINE_IO_WEB_SERVER_HPP_
#define SG_ENGINE_IO_WEB_SERVER_HPP_

// Static files plus one WebSocket endpoint, which is everything the browser
// needs: no internet, no framework, no build step. The simulator, the dashboard
// and later the team panels are all served from the same directory and talk
// over the same socket, one JSON object per frame.

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/json.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/error.hpp"

namespace sg
{

class WebServer
{
 public:
  // A frame that arrived from a browser. The handler runs on the one Asio
  // thread, like everything else that touches game state.
  using MessageHandler = std::function<void(const boost::json::object&)>;
  using ConnectHandler = std::function<void()>;

  WebServer(boost::asio::io_context& io, std::filesystem::path web_root);
  ~WebServer();

  WebServer(const WebServer&) = delete;
  WebServer(WebServer&&) = delete;
  auto operator=(const WebServer&) -> WebServer& = delete;
  auto operator=(WebServer&&) -> WebServer& = delete;

  // Starts accepting. Port 0 asks the system for a free one; the port actually
  // bound comes back, which is what the tests use.
  [[nodiscard]] auto Listen(std::uint16_t port) -> Result<std::uint16_t>;

  // Registers the handler for one "type" of inbound frame. A frame whose type
  // nobody handles is counted and dropped, never acted on.
  auto On(std::string_view type, MessageHandler handler) -> void;

  // Called whenever a browser connects, so the engine can push the current
  // state to a page that just opened.
  auto OnConnect(ConnectHandler handler) -> void;

  // Sends one frame to every connected browser.
  auto Broadcast(const boost::json::object& frame) -> void;

  [[nodiscard]] auto ClientCount() const -> std::size_t;
  [[nodiscard]] auto UnhandledFrames() const -> std::uint64_t
  {
    return m_unhandled;
  }

 private:
  class WsSession;
  class HttpSession;

  auto Accept() -> void;
  auto Dispatch(const boost::json::object& frame) -> void;
  auto Join(const std::shared_ptr<WsSession>& session) -> void;
  // Called by a session once its handshake is done, which is the first moment
  // it can be written to.
  auto Welcome() -> void;
  [[nodiscard]] auto Resolve(std::string_view target) const
      -> std::filesystem::path;

  boost::asio::io_context& m_io;
  std::filesystem::path m_web_root;
  boost::asio::ip::tcp::acceptor m_acceptor;
  std::map<std::string, MessageHandler, std::less<>> m_handlers;
  ConnectHandler m_on_connect;
  // Sessions keep themselves alive while an asynchronous operation is pending,
  // so the server holds weak references: a browser that closes its tab must not
  // be kept alive by this list.
  std::vector<std::weak_ptr<WsSession>> m_clients;
  std::uint64_t m_unhandled = 0;
};

}  // namespace sg

#endif  // SG_ENGINE_IO_WEB_SERVER_HPP_
