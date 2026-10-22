#ifndef SG_ENGINE_CORE_OPTIONAL_REF_HPP_
#define SG_ENGINE_CORE_OPTIONAL_REF_HPP_

// sg::OptionalRef<T> is "a reference that may be absent": what a lookup returns
// when the thing may not be there and the caller must not own it. P2988 makes
// std::optional<T&> exactly that, so where the standard library already has it
// this header is an alias and nothing else; libstdc++ 16 and libc++ 22 do.
//
// Until both minimum toolchains have it, the fallback below provides the same
// API subset. It is the only place in the repository allowed to store a raw
// pointer: everywhere else a borrowed thing is a reference, a span, a
// string_view or one of these.

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <version>

#if defined(__cpp_lib_optional_ref) && __cpp_lib_optional_ref >= 202602L
#define SG_HAS_STD_OPTIONAL_REF 1
#else
#define SG_HAS_STD_OPTIONAL_REF 0
#endif

namespace sg
{

#if SG_HAS_STD_OPTIONAL_REF

template <typename T>
using OptionalRef = std::optional<T&>;

#else

template <typename T>
class OptionalRef
{
 public:
  using value_type = T&;

  OptionalRef() noexcept = default;
  OptionalRef(std::nullopt_t) noexcept {
  }  // NOLINT(google-explicit-constructor)

  // Implicit, like std::optional<T&>: a function returning OptionalRef<Team>
  // can `return team;`.
  OptionalRef(T& value) noexcept  // NOLINT(google-explicit-constructor)
      : m_value(std::addressof(value))
  {
  }

  // Converting copy, so OptionalRef<Team> becomes OptionalRef<const Team>.
  template <typename U>
    requires(!std::is_same_v<U, T> && std::is_convertible_v<U*, T*>)
  OptionalRef(const OptionalRef<U>&
                  other) noexcept  // NOLINT(google-explicit-constructor)
      : m_value(other.has_value() ? std::addressof(*other) : nullptr)
  {
  }

  // Assignment rebinds, it does not assign through the reference: this is a
  // handle to an object, not the object.
  auto operator=(std::nullopt_t) noexcept -> OptionalRef&
  {
    m_value = nullptr;
    return *this;
  }

  auto operator=(T& value) noexcept -> OptionalRef&
  {
    m_value = std::addressof(value);
    return *this;
  }

  [[nodiscard]] auto has_value() const noexcept -> bool
  {
    return m_value != nullptr;
  }
  explicit operator bool() const noexcept
  {
    return has_value();
  }

  [[nodiscard]] auto operator*() const noexcept -> T&
  {
    return *m_value;
  }
  [[nodiscard]] auto operator->() const noexcept -> T*
  {
    return m_value;
  }

  template <typename U>
  [[nodiscard]] auto value_or(U&& fallback) const -> std::remove_cv_t<T>
  {
    return has_value()
               ? *m_value
               : static_cast<std::remove_cv_t<T> >(std::forward<U>(fallback));
  }

  // and_then expects a function returning another optional-like type; transform
  // wraps a plain result. Both do nothing when the reference is absent.
  template <typename F>
  [[nodiscard]] auto and_then(F&& f) const -> std::invoke_result_t<F, T&>
  {
    using Out = std::invoke_result_t<F, T&>;
    if (!has_value())
    {
      return Out{};
    }
    return std::invoke(std::forward<F>(f), *m_value);
  }

  template <typename F>
  [[nodiscard]] auto transform(F&& f) const
      -> std::optional<std::remove_cvref_t<std::invoke_result_t<F, T&> > >
  {
    using Out =
        std::optional<std::remove_cvref_t<std::invoke_result_t<F, T&> > >;
    if (!has_value())
    {
      return Out{};
    }
    return Out{std::invoke(std::forward<F>(f), *m_value)};
  }

 private:
  // The one raw pointer the guidelines allow, and the reason this header
  // exists.
  T* m_value = nullptr;
};

#endif  // SG_HAS_STD_OPTIONAL_REF

}  // namespace sg

#endif  // SG_ENGINE_CORE_OPTIONAL_REF_HPP_
