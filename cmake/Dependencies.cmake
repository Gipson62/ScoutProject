# Every version pinned here is recorded in docs/DEPENDENCIES.md.
include(FetchContent)

# Boost comes from system packages: headers only, no compiled component. Asio,
# Beast and JSON are all header-only, and Boost.JSON is compiled into the engine
# through <boost/json/src.hpp> in exactly one translation unit.
find_package(Boost 1.83 REQUIRED)
find_package(Threads REQUIRED)

set(SG_TOMLPLUSPLUS_TAG v3.4.0)
set(SG_POCKETPY_TAG v2.2.0)
set(SG_GOOGLETEST_TAG v1.17.0)

FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG ${SG_TOMLPLUSPLUS_TAG}
  GIT_SHALLOW TRUE
  SYSTEM)

# pocketpy is used through its C API only (pocketpy.h); its pybind11-compatible
# layer relies on C++ exceptions, which the engine is built without.
set(PK_BUILD_STATIC_LIB
    ON
    CACHE BOOL "" FORCE)
FetchContent_Declare(
  pocketpy
  GIT_REPOSITORY https://github.com/pocketpy/pocketpy.git
  GIT_TAG ${SG_POCKETPY_TAG}
  GIT_SHALLOW TRUE
  SYSTEM)

FetchContent_MakeAvailable(tomlplusplus pocketpy)

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
  set(gtest_force_shared_crt
      ON
      CACHE BOOL "" FORCE)
  set(INSTALL_GTEST
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG ${SG_GOOGLETEST_TAG}
    GIT_SHALLOW TRUE
    SYSTEM)
  FetchContent_MakeAvailable(googletest)
endif()
