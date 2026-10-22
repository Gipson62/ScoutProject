// The one translation unit that compiles Boost.JSON, which is used header-only:
// no libboost_json is linked, so the library's version can never disagree with
// the headers. Nothing else in the engine includes <boost/json/src.hpp>.
//
// BOOST_NO_EXCEPTIONS is defined for the whole engine, so the throw_exception
// overloads this object file needs come from engine/core/panic.cpp.
#include <boost/json/src.hpp>
