#ifndef SG_ENGINE_CORE_RANDOM_HPP_
#define SG_ENGINE_CORE_RANDOM_HPP_

// The only source of randomness game code may use. It is in the repository, and
// not std::mt19937 with a std::*_distribution, because a distribution's output
// is unspecified: libstdc++ and libc++ disagree, so the same log replayed on a
// different standard library would give a different game.

#include <cstdint>

namespace sg
{

// SplitMix64 (Steele, Lea and Flood, 2014): 64 bits of state, no warm-up, and a
// published constant stream, so a seed in a config file names one sequence for
// ever.
class Random
{
 public:
  explicit Random(std::uint64_t seed) : m_state(seed) {}

  auto NextU64() -> std::uint64_t;

  // Uniform in [0, bound), by rejection: taking the remainder would make the
  // low values slightly more likely, and a biased battle roll is a biased game.
  // A bound of 0 panics, because there is no value to return.
  auto NextBelow(std::uint64_t bound) -> std::uint64_t;

  // Uniform in [low, high], both ends included. low > high panics.
  auto NextInRange(std::int64_t low, std::int64_t high) -> std::int64_t;

  // Uniform in [0, 1), from the top 53 bits, which is the whole mantissa and no
  // more.
  auto NextDouble() -> double;

  // Uniform in [low, high). This is what a mode's `game.random.uniform` calls.
  auto NextUniform(double low, double high) -> double;

  [[nodiscard]] auto State() const -> std::uint64_t
  {
    return m_state;
  }

 private:
  std::uint64_t m_state = 0;
};

}  // namespace sg

#endif  // SG_ENGINE_CORE_RANDOM_HPP_
