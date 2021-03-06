// Copyright 2016-2017 Francesco Biscani (bluescarni@gmail.com)
//
// This file is part of the mp++ library.
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

// std::index_sequence and std::make_index_sequence implementation, from:
// http://stackoverflow.com/questions/17424477/implementation-c14-make-integer-sequence

#ifndef MPPP_TEST_UTILS_HPP
#define MPPP_TEST_UTILS_HPP

#include <cassert>
#include <cstddef>
#include <gmp.h>
#include <initializer_list>
#include <limits>
#include <locale>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <mp++/mp++.hpp>

namespace mppp_test
{

// std::index_sequence and std::make_index_sequence implementation for C++11. These are available
// in the std library in C++14. Implementation taken from:
// http://stackoverflow.com/questions/17424477/implementation-c14-make-integer-sequence
template <std::size_t... Ints>
struct index_sequence {
    using type = index_sequence;
    using value_type = std::size_t;
    static constexpr std::size_t size() noexcept
    {
        return sizeof...(Ints);
    }
};

inline namespace impl
{

template <class Sequence1, class Sequence2>
struct merge_and_renumber;

template <std::size_t... I1, std::size_t... I2>
struct merge_and_renumber<index_sequence<I1...>, index_sequence<I2...>>
    : index_sequence<I1..., (sizeof...(I1) + I2)...> {
};
}

template <std::size_t N>
struct make_index_sequence
    : merge_and_renumber<typename make_index_sequence<N / 2>::type, typename make_index_sequence<N - N / 2>::type> {
};

template <>
struct make_index_sequence<0> : index_sequence<> {
};
template <>
struct make_index_sequence<1> : index_sequence<0> {
};

inline namespace impl
{

template <typename T, typename F, std::size_t... Is>
void apply_to_each_item(T &&t, const F &f, index_sequence<Is...>)
{
    (void)std::initializer_list<int>{0, (void(f(std::get<Is>(std::forward<T>(t)))), 0)...};
}
}

// Tuple for_each(). Execute the functor f on each element of the input Tuple.
// https://isocpp.org/blog/2015/01/for-each-arg-eric-niebler
// https://www.reddit.com/r/cpp/comments/2tffv3/for_each_argumentsean_parent/
// https://www.reddit.com/r/cpp/comments/33b06v/for_each_in_tuple/
template <class Tuple, class F>
void tuple_for_each(Tuple &&t, const F &f)
{
    apply_to_each_item(std::forward<Tuple>(t), f,
                       make_index_sequence<std::tuple_size<typename std::decay<Tuple>::type>::value>{});
}

inline namespace impl
{

template <typename T>
struct is_integer {
    static const bool value = false;
};

template <std::size_t SSize>
struct is_integer<mppp::integer<SSize>> {
    static const bool value = true;
};

template <typename T, typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value, int>::type = 0>
inline long long lex_cast_tr(T n)
{
    return static_cast<long long>(n);
}

template <typename T, typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value, int>::type = 0>
inline unsigned long long lex_cast_tr(T n)
{
    return static_cast<unsigned long long>(n);
}

template <typename T, typename std::enable_if<is_integer<T>::value, int>::type = 0>
inline std::string lex_cast_tr(const T &x)
{
    return x.to_string();
}

template <std::size_t SSize>
inline std::string lex_cast_tr(const mppp::rational<SSize> &q)
{
    return q.to_string();
}

template <typename T, typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
inline T lex_cast_tr(const T &x)
{
    return x;
}
}

// Lexical cast: retrieve the string representation of input object x.
template <typename T>
inline std::string lex_cast(const T &x)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << lex_cast_tr(x);
    return oss.str();
}

inline std::string lex_cast(const mppp::mpz_raii &m)
{
    return mppp::mpz_to_str(&m.m_mpz);
}

inline std::string lex_cast(const mppp::mpq_raii &m)
{
    return mppp::rational<1>(&m.m_mpq).to_string();
}

// Set mpz to random value with n limbs. Top limb is divided by div.
inline void random_integer(mppp::mpz_raii &m, unsigned n, std::mt19937 &rng, ::mp_limb_t div = 1u)
{
    if (!n) {
        ::mpz_set_ui(&m.m_mpz, 0);
        return;
    }
    MPPP_MAYBE_TLS mppp::mpz_raii tmp;
    std::uniform_int_distribution<::mp_limb_t> dist(0u, std::numeric_limits<::mp_limb_t>::max());
    // Set the first limb.
    ::mpz_set_str(&m.m_mpz, lex_cast((dist(rng) & GMP_NUMB_MASK) / div).c_str(), 10);
    for (unsigned i = 1u; i < n; ++i) {
        ::mpz_set_str(&tmp.m_mpz, lex_cast(dist(rng) & GMP_NUMB_MASK).c_str(), 10);
        ::mpz_mul_2exp(&m.m_mpz, &m.m_mpz, GMP_NUMB_BITS);
        ::mpz_add(&m.m_mpz, &m.m_mpz, &tmp.m_mpz);
    }
}

// Set mpq to random value with n limbs for num/den.
inline void random_rational(mppp::mpq_raii &m, unsigned n, std::mt19937 &rng)
{
    if (!n) {
        ::mpq_set_ui(&m.m_mpq, 0, 1);
        return;
    }
    MPPP_MAYBE_TLS mppp::mpz_raii tmp;
    std::uniform_int_distribution<::mp_limb_t> dist(0u, std::numeric_limits<::mp_limb_t>::max());
    // Set the first limb.
    ::mpz_set_str(mpq_numref(&m.m_mpq), lex_cast(dist(rng) & GMP_NUMB_MASK).c_str(), 10);
    ::mpz_set_str(mpq_denref(&m.m_mpq), lex_cast(dist(rng) & GMP_NUMB_MASK).c_str(), 10);
    for (unsigned i = 1u; i < n; ++i) {
        ::mpz_set_str(&tmp.m_mpz, lex_cast(dist(rng) & GMP_NUMB_MASK).c_str(), 10);
        ::mpz_mul_2exp(mpq_numref(&m.m_mpq), mpq_numref(&m.m_mpq), GMP_NUMB_BITS);
        ::mpz_add(mpq_numref(&m.m_mpq), mpq_numref(&m.m_mpq), &tmp.m_mpz);
        ::mpz_set_str(&tmp.m_mpz, lex_cast(dist(rng) & GMP_NUMB_MASK).c_str(), 10);
        ::mpz_mul_2exp(mpq_denref(&m.m_mpq), mpq_denref(&m.m_mpq), GMP_NUMB_BITS);
        ::mpz_add(mpq_denref(&m.m_mpq), mpq_denref(&m.m_mpq), &tmp.m_mpz);
    }
    // Take care of zero den.
    if (mpz_sgn(mpq_denref(&m.m_mpq)) == 0) {
        ::mpz_set_ui(mpq_denref(&m.m_mpq), 1);
    }
    ::mpq_canonicalize(&m.m_mpq);
}

// Set mpz to the max value with n limbs.
inline void max_integer(mppp::mpz_raii &m, unsigned n)
{
    if (!n) {
        ::mpz_set_ui(&m.m_mpz, 0);
        return;
    }
    MPPP_MAYBE_TLS mppp::mpz_raii tmp;
    // Set the first limb.
    ::mpz_set_str(&m.m_mpz, lex_cast(::mp_limb_t(-1) & GMP_NUMB_MASK).c_str(), 10);
    for (unsigned i = 1u; i < n; ++i) {
        ::mpz_set_str(&tmp.m_mpz, lex_cast(::mp_limb_t(-1) & GMP_NUMB_MASK).c_str(), 10);
        ::mpz_mul_2exp(&m.m_mpz, &m.m_mpz, GMP_NUMB_BITS);
        ::mpz_add(&m.m_mpz, &m.m_mpz, &tmp.m_mpz);
    }
}
}

// A macro for checking that an expression throws a specific exception object satisfying a predicate.
#define REQUIRE_THROWS_PREDICATE(expr, exc, pred)                                                                      \
    {                                                                                                                  \
        bool thrown_checked = false;                                                                                   \
        bool pred_checked = false;                                                                                     \
        try {                                                                                                          \
            (void)(expr);                                                                                              \
        } catch (const exc &e) {                                                                                       \
            thrown_checked = true;                                                                                     \
            if (pred(e)) {                                                                                             \
                pred_checked = true;                                                                                   \
            }                                                                                                          \
        }                                                                                                              \
        REQUIRE(thrown_checked);                                                                                       \
        REQUIRE(pred_checked);                                                                                         \
    }

#endif
