#ifndef SG_ENGINE_IO_SIM_TRANSPORT_HPP_
#define SG_ENGINE_IO_SIM_TRANSPORT_HPP_

// The capture points, played by the browser. web/sim.html sends a "sim.tap" or
// "sim.button" frame; the LEDs and beeps go back the same way.

#include "engine/core/clock.hpp"
#include "engine/core/event.hpp"
#include "engine/io/transport.hpp"
#include "engine/io/web_server.hpp"

namespace sg
{

class SimTransport final : public Transport
{
 public:
  SimTransport(WebServer& web, Clock& clock) : m_web(web), m_clock(clock) {}

  auto Start(InputHandler on_input) -> void override;
  auto Send(const LedState& state) -> void override;
  auto Send(const Beep& beep) -> void override;

  // Frames that named no point, or a field of the wrong type: the browser is
  // not to be trusted blindly, and a bad frame is counted rather than acted on.
  [[nodiscard]] auto RejectedFrames() const -> std::uint64_t
  {
    return m_rejected;
  }

 private:
  WebServer& m_web;
  Clock& m_clock;
  std::uint64_t m_rejected = 0;
};

}  // namespace sg

#endif  // SG_ENGINE_IO_SIM_TRANSPORT_HPP_
