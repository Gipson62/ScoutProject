#ifndef SG_ENGINE_IO_OUTPUT_SINK_HPP_
#define SG_ENGINE_IO_OUTPUT_SINK_HPP_

// Where the Core's commands go in Phase 1: a point's LED and buzzer to the
// transport, everything else to the browser. Phase 3 replaces this with the
// full fan-out of DESIGN.md's CommandRouter — map pixels to the station, sounds
// to miniaudio, panels to the panel pages — without the Core changing.

#include "engine/core/command_router.hpp"
#include "engine/core/event.hpp"
#include "engine/io/transport.hpp"
#include "engine/io/web_server.hpp"

namespace sg
{

class OutputSink final : public CommandSink
{
 public:
  OutputSink(Transport& transport, WebServer& web)
      : m_transport(transport), m_web(web)
  {
  }

  auto Send(const Command& command) -> void override;

 private:
  Transport& m_transport;
  WebServer& m_web;
};

}  // namespace sg

#endif  // SG_ENGINE_IO_OUTPUT_SINK_HPP_
