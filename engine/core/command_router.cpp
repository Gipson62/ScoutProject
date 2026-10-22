#include "engine/core/command_router.hpp"

#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"

namespace sg
{

auto CommandRouter::Send(const Command& command) -> void
{
  if (m_muted)
  {
    return;
  }
  m_sink.Send(command);
}

auto CommandRouter::PushFullState(const GameState& state) -> void
{
  for (const PointState& point : state.Points())
  {
    Send(Command{point.led});
  }
}

}  // namespace sg
