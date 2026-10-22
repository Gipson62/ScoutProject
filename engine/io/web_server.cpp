#include "engine/io/web_server.hpp"

#include <algorithm>
#include <boost/asio/ip/tcp.hpp>
#include <boost/json.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/core/error.hpp"
#include "engine/io/beast.hpp"

namespace sg
{
namespace
{

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

constexpr const char* kWebSocketPath = "/ws";
constexpr const char* kDefaultPage = "sim.html";

[[nodiscard]] auto MimeTypeOf(const std::filesystem::path& path)
    -> std::string_view
{
  const std::string extension = path.extension().string();
  if (extension == ".html")
  {
    return "text/html; charset=utf-8";
  }
  if (extension == ".css")
  {
    return "text/css; charset=utf-8";
  }
  if (extension == ".js")
  {
    return "application/javascript; charset=utf-8";
  }
  if (extension == ".json")
  {
    return "application/json";
  }
  if (extension == ".svg")
  {
    return "image/svg+xml";
  }
  if (extension == ".png")
  {
    return "image/png";
  }
  if (extension == ".wav")
  {
    return "audio/wav";
  }
  return "application/octet-stream";
}

}  // namespace

// One connected browser. It keeps itself alive through shared_from_this while
// any asynchronous operation is pending — the usual reason for shared_ptr, and
// the only one in the engine.
class WebServer::WsSession : public std::enable_shared_from_this<WsSession>
{
 public:
  WsSession(tcp::socket socket, WebServer& server)
      : m_ws(std::move(socket)), m_server(server)
  {
  }

  auto Run(http::request<http::string_body> request) -> void
  {
    m_ws.async_accept(request,
                      [self = shared_from_this()](beast::error_code ec)
                      {
                        if (ec)
                        {
                          return;
                        }
                        // Only now can this session be written to, so this is
                        // also when the engine is told a page has opened: a
                        // welcome frame sent before the handshake finishes
                        // would be thrown away.
                        self->m_ready = true;
                        self->m_server.Welcome();
                        self->Read();
                      });
  }

  auto Send(std::shared_ptr<const std::string> text) -> void
  {
    if (!m_ready)
    {
      // Still handshaking. Dropping is right rather than queueing: the welcome
      // frame that follows the handshake carries the whole state anyway.
      return;
    }
    m_queue.push_back(std::move(text));
    if (m_queue.size() == 1)
    {
      Write();
    }
  }

  [[nodiscard]] auto Open() const -> bool
  {
    return m_ready && m_ws.is_open();
  }

 private:
  auto Read() -> void
  {
    m_ws.async_read(
        m_buffer,
        [self = shared_from_this()](beast::error_code ec, std::size_t)
        {
          if (ec)
          {
            return;  // the tab was closed, or the network went away
          }
          self->OnFrame();
          self->Read();
        });
  }

  auto OnFrame() -> void
  {
    const std::string text = beast::buffers_to_string(m_buffer.data());
    m_buffer.consume(m_buffer.size());

    boost::system::error_code ec;
    const boost::json::value value = boost::json::parse(text, ec);
    if (ec || !value.is_object())
    {
      ++m_server.m_unhandled;
      return;
    }
    m_server.Dispatch(value.get_object());
  }

  auto Write() -> void
  {
    m_ws.text(true);
    m_ws.async_write(
        boost::asio::buffer(*m_queue.front()),
        [self = shared_from_this()](beast::error_code ec, std::size_t)
        {
          self->m_queue.pop_front();
          if (ec)
          {
            return;
          }
          if (!self->m_queue.empty())
          {
            self->Write();
          }
        });
  }

  websocket::stream<tcp::socket> m_ws;
  WebServer& m_server;
  beast::flat_buffer m_buffer;
  // Frames are shared because one Broadcast hands the same text to every
  // client, and each client writes it on its own schedule.
  std::deque<std::shared_ptr<const std::string>> m_queue;
  bool m_ready = false;
};

// One HTTP request: either a WebSocket upgrade, which becomes a WsSession, or a
// file from the web directory.
class WebServer::HttpSession : public std::enable_shared_from_this<HttpSession>
{
 public:
  HttpSession(tcp::socket socket, WebServer& server)
      : m_socket(std::move(socket)), m_server(server)
  {
  }

  auto Run() -> void
  {
    Read();
  }

 private:
  auto Read() -> void
  {
    m_request = {};
    http::async_read(
        m_socket, m_buffer, m_request,
        [self = shared_from_this()](beast::error_code ec, std::size_t)
        {
          if (ec)
          {
            return;
          }
          self->Handle();
        });
  }

  auto Handle() -> void
  {
    if (websocket::is_upgrade(m_request))
    {
      auto session = std::make_shared<WsSession>(std::move(m_socket), m_server);
      m_server.Join(session);
      session->Run(std::move(m_request));
      return;
    }

    if (m_request.method() != http::verb::get &&
        m_request.method() != http::verb::head)
    {
      return Fail(http::status::method_not_allowed, "only GET is served here");
    }

    const std::filesystem::path path = m_server.Resolve(m_request.target());
    if (path.empty())
    {
      return Fail(http::status::not_found, "no such file");
    }

    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.string().c_str(), beast::file_mode::scan, ec);
    if (ec)
    {
      return Fail(http::status::not_found, "no such file");
    }

    const std::uint64_t size = body.size();
    auto response = std::make_shared<http::response<http::file_body>>(
        std::piecewise_construct, std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, m_request.version()));
    response->set(http::field::content_type, MimeTypeOf(path));
    // Pages are edited while the engine runs, so a cached copy is worse than a
    // second request.
    response->set(http::field::cache_control, "no-store");
    response->content_length(size);
    response->keep_alive(m_request.keep_alive());

    http::async_write(m_socket, *response,
                      [self = shared_from_this(), response](
                          beast::error_code write_ec, std::size_t)
                      {
                        if (write_ec || !response->keep_alive())
                        {
                          beast::error_code ignored;
                          static_cast<void>(self->m_socket.shutdown(
                              tcp::socket::shutdown_send, ignored));
                          return;
                        }
                        self->Read();
                      });
  }

  auto Fail(http::status status, std::string_view why) -> void
  {
    auto response = std::make_shared<http::response<http::string_body>>(
        status, m_request.version());
    response->set(http::field::content_type, "text/plain; charset=utf-8");
    response->body() = std::string{why};
    response->prepare_payload();
    http::async_write(
        m_socket, *response,
        [self = shared_from_this(), response](beast::error_code, std::size_t)
        {
          beast::error_code ignored;
          static_cast<void>(
              self->m_socket.shutdown(tcp::socket::shutdown_send, ignored));
        });
  }

  tcp::socket m_socket;
  WebServer& m_server;
  beast::flat_buffer m_buffer;
  http::request<http::string_body> m_request;
};

WebServer::WebServer(boost::asio::io_context& io,
                     std::filesystem::path web_root)
    : m_io(io), m_web_root(std::move(web_root)), m_acceptor(io)
{
}

WebServer::~WebServer() = default;

auto WebServer::Listen(std::uint16_t port) -> Result<std::uint16_t>
{
  std::error_code fs_ec;
  if (!std::filesystem::is_directory(m_web_root, fs_ec))
  {
    return Fail(ErrorCode::kIoWebRootMissing,
                "not a directory: " + m_web_root.string());
  }
  m_web_root = std::filesystem::weakly_canonical(m_web_root, fs_ec);
  if (fs_ec)
  {
    return Fail(ErrorCode::kIoWebRootMissing,
                "cannot resolve " + m_web_root.string());
  }

  const tcp::endpoint endpoint{boost::asio::ip::make_address_v4("0.0.0.0"),
                               port};
  beast::error_code ec;
  static_cast<void>(m_acceptor.open(endpoint.protocol(), ec));
  if (ec)
  {
    return Fail(ErrorCode::kIoListenFailed, "open: " + ec.message());
  }
  static_cast<void>(
      m_acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec));
  if (ec)
  {
    return Fail(ErrorCode::kIoListenFailed, "reuse_address: " + ec.message());
  }
  static_cast<void>(m_acceptor.bind(endpoint, ec));
  if (ec)
  {
    return Fail(ErrorCode::kIoListenFailed,
                "port " + std::to_string(port) + ": " + ec.message());
  }
  static_cast<void>(
      m_acceptor.listen(boost::asio::socket_base::max_listen_connections, ec));
  if (ec)
  {
    return Fail(ErrorCode::kIoListenFailed, "listen: " + ec.message());
  }

  const tcp::endpoint bound = m_acceptor.local_endpoint(ec);
  if (ec)
  {
    return Fail(ErrorCode::kIoListenFailed, "local_endpoint: " + ec.message());
  }

  Accept();
  return bound.port();
}

auto WebServer::Accept() -> void
{
  m_acceptor.async_accept(
      m_io,
      [this](beast::error_code ec, tcp::socket socket)
      {
        if (ec)
        {
          return;  // the acceptor was closed
        }
        std::make_shared<HttpSession>(std::move(socket), *this)->Run();
        Accept();
      });
}

auto WebServer::On(std::string_view type, MessageHandler handler) -> void
{
  m_handlers[std::string{type}] = std::move(handler);
}

auto WebServer::OnConnect(ConnectHandler handler) -> void
{
  m_on_connect = std::move(handler);
}

auto WebServer::Dispatch(const boost::json::object& frame) -> void
{
  const boost::json::value* type = frame.if_contains("type");
  if (type == nullptr || !type->is_string())
  {
    ++m_unhandled;
    return;
  }
  const auto found = m_handlers.find(std::string_view{type->get_string()});
  if (found == m_handlers.end())
  {
    ++m_unhandled;
    return;
  }
  found->second(frame);
}

auto WebServer::Join(const std::shared_ptr<WsSession>& session) -> void
{
  // Expired entries are cleared here rather than on close: there is no
  // disconnect callback to hook, and a connect is the natural moment.
  std::erase_if(m_clients,
                [](const std::weak_ptr<WsSession>& client)
                {
                  return client.expired();
                });
  m_clients.push_back(session);
}

auto WebServer::Welcome() -> void
{
  if (m_on_connect)
  {
    m_on_connect();
  }
}

auto WebServer::Broadcast(const boost::json::object& frame) -> void
{
  if (m_clients.empty())
  {
    return;
  }
  auto text =
      std::make_shared<const std::string>(boost::json::serialize(frame));
  for (const std::weak_ptr<WsSession>& client : m_clients)
  {
    if (const std::shared_ptr<WsSession> session = client.lock())
    {
      session->Send(text);
    }
  }
}

auto WebServer::ClientCount() const -> std::size_t
{
  return static_cast<std::size_t>(std::ranges::count_if(
      m_clients,
      [](const std::weak_ptr<WsSession>& client)
      {
        const std::shared_ptr<WsSession> session = client.lock();
        return session && session->Open();
      }));
}

auto WebServer::Resolve(std::string_view target) const -> std::filesystem::path
{
  if (target.empty() || target.front() != '/' || target == kWebSocketPath)
  {
    return {};
  }
  // A query string names no file.
  if (const std::size_t question = target.find('?');
      question != std::string_view::npos)
  {
    target = target.substr(0, question);
  }
  std::filesystem::path relative{target.substr(1)};
  if (relative.empty())
  {
    relative = kDefaultPage;
  }
  if (relative.is_absolute())
  {
    return {};
  }

  std::error_code ec;
  const std::filesystem::path candidate =
      std::filesystem::weakly_canonical(m_web_root / relative, ec);
  if (ec)
  {
    return {};
  }
  // ".." in a URL must not reach outside the web directory, whatever the
  // browser or a curious scout types.
  const std::string root = m_web_root.string();
  const std::string resolved = candidate.string();
  if (!resolved.starts_with(root))
  {
    return {};
  }
  if (!std::filesystem::is_regular_file(candidate, ec))
  {
    return {};
  }
  return candidate;
}

}  // namespace sg
