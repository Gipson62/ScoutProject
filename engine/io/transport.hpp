#ifndef SG_ENGINE_IO_TRANSPORT_HPP_
#define SG_ENGINE_IO_TRANSPORT_HPP_

// The one seam that changes between the laptop and the camp. SimTransport
// implements it now with the browser playing the capture points; February's
// SerialTransport implements it over the LoRa hub, and nothing above it
// changes.

#include <functional>

#include "engine/core/event.hpp"

namespace sg
{

class Transport
{
 public:
  // Called with every tap and button press the field produces.
  using InputHandler = std::function<void(Event)>;

  Transport() = default;
  Transport(const Transport&) = default;
  Transport(Transport&&) = default;
  auto operator=(const Transport&) -> Transport& = default;
  auto operator=(Transport&&) -> Transport& = default;
  virtual ~Transport() = default;

  virtual auto Start(InputHandler on_input) -> void = 0;
  virtual auto Send(const LedState& state) -> void = 0;
  virtual auto Send(const Beep& beep) -> void = 0;
};

}  // namespace sg

#endif  // SG_ENGINE_IO_TRANSPORT_HPP_
