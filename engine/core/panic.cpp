#include "engine/core/panic.hpp"

#include <boost/assert/source_location.hpp>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>
#include <source_location>
#include <string_view>

namespace sg
{

auto Panic(std::string_view message, std::source_location location) -> void
{
  std::println(stderr, "sgengine panic: {}\n  at {}:{}:{} in {}", message,
               location.file_name(), location.line(), location.column(),
               location.function_name());
  std::fflush(stderr);
  std::abort();
}

}  // namespace sg

// Boost is built here with BOOST_NO_EXCEPTIONS, which means Boost calls
// boost::throw_exception and the program must supply it. Both overloads are
// required, and reaching either means a Boost call that can throw was used
// where its error_code overload should have been: that is a bug in our code, so
// it panics rather than returning.
namespace boost
{

auto throw_exception(const std::exception& e) -> void
{
  ::sg::Panic(e.what());
}

auto throw_exception(const std::exception& e, const boost::source_location& loc)
    -> void
{
  ::sg::Panic(e.what(), std::source_location::current());
  static_cast<void>(loc);
}

}  // namespace boost
