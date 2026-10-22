#ifndef SG_ENGINE_IO_BEAST_HPP_
#define SG_ENGINE_IO_BEAST_HPP_

// Boost.Beast, included in one place.
//
// Beast 1.83 still uses std::aligned_storage, which C++23 deprecates, so
// -Wdeprecated-declarations fires inside its headers and -Werror stops the
// build. The warning is about Beast's own code and there is nothing to fix in
// it, so it is silenced here, around these includes only, rather than turned
// off for the whole engine: our own deprecated calls still fail the build.

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif  // SG_ENGINE_IO_BEAST_HPP_
