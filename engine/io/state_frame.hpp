#ifndef SG_ENGINE_IO_STATE_FRAME_HPP_
#define SG_ENGINE_IO_STATE_FRAME_HPP_

// The one frame a page needs when it opens: who the teams are, what the points
// are, who holds them and what their lights are doing. Everything after it
// arrives as the small frames the Core already produces, so a page that misses
// this frame shows nothing rather than something wrong.

#include <boost/json.hpp>
#include <span>

#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/mode.hpp"

namespace sg
{

[[nodiscard]] auto StateFrame(const GameState& state, Millis now,
                              std::span<const Tag> wristbands)
    -> boost::json::object;

// One logged event, for the dashboard's feed. The sequence number is what the
// Game Master would void, so it is part of the frame.
[[nodiscard]] auto EventFrame(const Event& event) -> boost::json::object;

}  // namespace sg

#endif  // SG_ENGINE_IO_STATE_FRAME_HPP_
