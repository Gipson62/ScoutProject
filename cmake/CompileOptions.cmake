# sg_options carries the rules of docs/CODING_GUIDELINES.md that the compiler can
# enforce. It is an INTERFACE target so it applies to our targets only: the
# fetched dependencies are built with their own flags and their warnings are not
# ours to fix.
add_library(sg_options INTERFACE)

target_compile_features(sg_options INTERFACE cxx_std_23)

target_compile_options(
  sg_options
  INTERFACE -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Werror
            # Section 7: errors are values, so the engine needs no unwinder.
            -fno-exceptions)

target_compile_definitions(
  sg_options
  INTERFACE # Boost must not throw: both boost::throw_exception overloads are
            # implemented in engine/core and call sg::Panic.
            BOOST_NO_EXCEPTIONS
            # toml++ returns a parse_result instead of throwing.
            TOML_EXCEPTIONS=0)
