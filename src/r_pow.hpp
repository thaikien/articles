#pragma once


namespace impl {

template <typename T, size_t Exp>
struct r_pow
{
  static T op(T base) { return r_pow<T, Exp - 1>::op(base) * base; }
};

template <typename T>
struct r_pow<T, 0>
{
  static T op(T) { return {1}; };
};

} // namespace impl

/// std::pow by unrolling loop at compile time
template <size_t Exp, typename T>
T r_pow(T base)
{
  return impl::r_pow<T, Exp>::op(base);
}
