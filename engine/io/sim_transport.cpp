#include "engine/io/sim_transport.hpp"

#include <boost/json.hpp>
#include <string>
#include <utility>

#include "engine/core/event.hpp"

namespace sg
{
namespace
{

[[nodiscard]] auto StringField(const boost::json::object& frame,
                               const char* key) -> std::string
{
  const boost::json::value* value = frame.if_contains(key);
  if (value == nullptr || !value->is_string())
  {
    return {};
  }
  return std::string{value->get_string().c_str()};
}

}  // namespace

auto SimTransport::Start(InputHandler on_input) -> void
{
  m_web.On("sim.tap",
           [this, on_input](const boost::json::object& frame)
           {
             const std::string point = StringField(frame, "point");
             const std::string uid = StringField(frame, "uid");
             if (point.empty() || uid.empty())
             {
               ++m_rejected;
               return;
             }
             // The simulator's taps happen now; a real tap carries its own age,
             // which is what February's SerialTransport subtracts instead.
             on_input(Event{0, m_clock.Now(), Tap{point, uid}});
           });

  m_web.On("sim.button",
           [this, on_input](const boost::json::object& frame)
           {
             const std::string point = StringField(frame, "point");
             if (point.empty())
             {
               ++m_rejected;
               return;
             }
             on_input(Event{0, m_clock.Now(), Button{point}});
           });
}

auto SimTransport::Send(const LedState& state) -> void
{
  m_web.Broadcast(ToJson(Command{state}));
}

auto SimTransport::Send(const Beep& beep) -> void
{
  m_web.Broadcast(ToJson(Command{beep}));
}

}  // namespace sg
