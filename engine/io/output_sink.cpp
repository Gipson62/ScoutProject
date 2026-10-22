#include "engine/io/output_sink.hpp"

#include <variant>

#include "engine/core/event.hpp"

namespace sg
{

auto OutputSink::Send(const Command& command) -> void
{
  std::visit(
      [this, &command](const auto& data)
      {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, LedState> || std::is_same_v<T, Beep>)
        {
          m_transport.Send(data);
          // The dashboard follows the same LEDs and beeps as the simulator, and
          // in Phase 1 both pages share one socket, so the broadcast the
          // transport already made serves both.
        }
        else
        {
          // Sounds, map pixels, panel pushes and announcements have no hardware
          // yet: the browser shows them.
          m_web.Broadcast(ToJson(command));
        }
      },
      command);
}

}  // namespace sg
