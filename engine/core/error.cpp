#include "engine/core/error.hpp"

#include <string_view>

namespace sg
{

auto Describe(ErrorCode code) -> std::string_view
{
  switch (code)
  {
    case ErrorCode::kNone:
      return "no error";
    case ErrorCode::kMissingField:
      return "a required field is missing";
    case ErrorCode::kWrongType:
      return "a field has the wrong type";
    case ErrorCode::kUnknownKind:
      return "unknown kind";
    case ErrorCode::kBadValue:
      return "a field has a value outside its range";
    case ErrorCode::kLogOpenFailed:
      return "the session log cannot be opened";
    case ErrorCode::kLogWriteFailed:
      return "the session log cannot be written";
    case ErrorCode::kLogCorruptLine:
      return "a line of the session log cannot be read";
  }
  return "unrecognised error code";
}

}  // namespace sg
