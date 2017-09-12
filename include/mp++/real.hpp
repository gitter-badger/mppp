// Copyright 2016-2017 Francesco Biscani (bluescarni@gmail.com)
//
// This file is part of the mp++ library.
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef MPPP_REAL_HPP
#define MPPP_REAL_HPP

#include <mp++/config.hpp>

#if defined(MPPP_WITH_MPFR)

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>

#include <mp++/concepts.hpp>
#include <mp++/detail/fwd_decl.hpp>
#include <mp++/detail/gmp.hpp>
#include <mp++/detail/mpfr.hpp>
#include <mp++/detail/type_traits.hpp>
#include <mp++/detail/utils.hpp>
#include <mp++/integer.hpp>
#include <mp++/rational.hpp>
#if defined(MPPP_WITH_QUADMATH)
#include <mp++/real128.hpp>
#endif

namespace mppp
{

inline namespace detail
{

// Clamp the MPFR precision between the min and max allowed values. This is used in the generic constructor.
constexpr ::mpfr_prec_t clamp_mpfr_prec(::mpfr_prec_t p)
{
    return mpfr_prec_check(p) ? p : (p < mpfr_prec_min() ? mpfr_prec_min() : mpfr_prec_max());
}

// Utility function to determine the number of base-2 digits of the significand
// of a floating-point type which is not in base-2.
template <typename T>
inline ::mpfr_prec_t dig2mpfr_prec()
{
    static_assert(std::is_floating_point<T>::value, "Invalid type.");
    // NOTE: just do a raw cast for the time being, it's not like we have ways of testing
    // this in any case. In the future we could consider switching to a compile-time implementation
    // of the integral log2, and do everything as compile-time integral computations.
    return static_cast<::mpfr_prec_t>(
        std::ceil(std::numeric_limits<T>::digits * std::log2(std::numeric_limits<T>::radix)));
}

// Helper function to print an mpfr to stream in base 10.
inline void mpfr_to_stream(const ::mpfr_t r, std::ostream &os)
{
    // Special values first.
    if (mpfr_nan_p(r)) {
        os << "nan";
        return;
    }
    if (mpfr_inf_p(r)) {
        if (mpfr_sgn(r) < 0) {
            os << "-";
        }
        os << "inf";
        return;
    }

    // Get the string fractional representation via the MPFR function,
    // and wrap it into a smart pointer.
    ::mpfr_exp_t exp(0);
    smart_mpfr_str str(::mpfr_get_str(nullptr, &exp, 10, 0, r, MPFR_RNDN), ::mpfr_free_str);
    if (mppp_unlikely(!str)) {
        throw std::runtime_error("Error in the conversion of a real to string: the call to mpfr_get_str() failed");
    }

    // Print the string, inserting a decimal point after the first digit.
    bool dot_added = false;
    for (auto cptr = str.get(); *cptr != '\0'; ++cptr) {
        os << (*cptr);
        // NOTE: check this answer:
        // http://stackoverflow.com/questions/13827180/char-ascii-relation
        // """
        // The mapping of integer values for characters does have one guarantee given
        // by the Standard: the values of the decimal digits are continguous.
        // (i.e., '1' - '0' == 1, ... '9' - '0' == 9)
        // """
        if (!dot_added && *cptr >= '0' && *cptr <= '9') {
            os << '.';
            dot_added = true;
        }
    }
    assert(dot_added);

    // Adjust the exponent. Do it in multiprec in order to avoid potential overflow.
    integer<1> z_exp{exp};
    --z_exp;
    const auto z_sgn = z_exp.sgn();
    if (z_sgn && !mpfr_zero_p(r)) {
        // Add the exponent at the end of the string, if both the value and the exponent
        // are nonzero.
        os << 'e';
        if (z_sgn == 1) {
            // Add extra '+' if the exponent is positive, for consistency with
            // real128's string format (and possibly other formats too?).
            os << '+';
        }
        os << z_exp;
    }
}
}

template <typename T>
using is_real_interoperable = disjunction<is_cpp_interoperable<T>, is_integer<T>, is_rational<T>
#if defined(MPPP_WITH_QUADMATH)
                                          ,
                                          std::is_same<T, real128>
#endif
                                          >;

template <typename T>
#if defined(MPPP_HAVE_CONCEPTS)
concept bool RealInteroperable = is_real_interoperable<T>::value;
#else
using real_interoperable_enabler = enable_if_t<is_real_interoperable<T>::value, int>;
#endif

class real
{
public:
    real() : real(0.)
    {
    }

private:
    // Utility function to check the precision upon init.
    static ::mpfr_prec_t check_init_prec(::mpfr_prec_t p)
    {
        if (mppp_unlikely(!mpfr_prec_check(p))) {
            throw std::invalid_argument("Cannot init a real with a precision of " + std::to_string(p)
                                        + ": the maximum allowed precision is " + std::to_string(mpfr_prec_max())
                                        + ", the minimum allowed precision is " + std::to_string(mpfr_prec_min()));
        }
        return p;
    }
    // Construction from FPs.
    template <typename T>
    using fp_a_ptr = int (*)(mpfr_t, T, ::mpfr_rnd_t);
    template <typename T>
    void dispatch_fp_construction(fp_a_ptr<T> ptr, const T &x, ::mpfr_prec_t p)
    {
        static_assert(std::numeric_limits<T>::digits <= std::numeric_limits<::mpfr_prec_t>::max(), "Overflow error.");
        ::mpfr_prec_t prec;
        if (p) {
            prec = check_init_prec(p);
        } else {
            prec = clamp_mpfr_prec(std::numeric_limits<T>::radix == 2
                                       ? static_cast<::mpfr_prec_t>(std::numeric_limits<T>::digits)
                                       : dig2mpfr_prec<T>());
        }
        ::mpfr_init2(&m_mpfr, prec);
        ptr(&m_mpfr, x, MPFR_RNDN);
    }
    void dispatch_construction(const float &x, ::mpfr_prec_t p)
    {
        dispatch_fp_construction(::mpfr_set_flt, x, p);
    }
    void dispatch_construction(const double &x, ::mpfr_prec_t p)
    {
        dispatch_fp_construction(::mpfr_set_d, x, p);
    }
    void dispatch_construction(const long double &x, ::mpfr_prec_t p)
    {
        dispatch_fp_construction(::mpfr_set_ld, x, p);
    }
    // Construction from integral types.
    // Special casing for bool.
    void dispatch_construction(const bool &b, ::mpfr_prec_t p)
    {
        dispatch_integral_init<bool>(p);
        ::mpfr_set_ui(&m_mpfr, static_cast<unsigned long>(b), MPFR_RNDN);
    }
    template <typename T>
    void dispatch_integral_init(::mpfr_prec_t p)
    {
        static_assert(std::numeric_limits<T>::digits <= std::numeric_limits<::mpfr_prec_t>::max(), "Overflow error.");
        ::mpfr_prec_t prec;
        if (p) {
            prec = check_init_prec(p);
        } else {
            prec = clamp_mpfr_prec(static_cast<::mpfr_prec_t>(std::numeric_limits<T>::digits));
        }
        ::mpfr_init2(&m_mpfr, prec);
    }
    template <typename T, enable_if_t<conjunction<std::is_integral<T>, std::is_unsigned<T>>::value, int> = 0>
    void dispatch_construction(const T &n, ::mpfr_prec_t p)
    {
        dispatch_integral_init<T>(p);
        if (n <= std::numeric_limits<unsigned long>::max()) {
            ::mpfr_set_ui(&m_mpfr, static_cast<unsigned long>(n), MPFR_RNDN);
        } else {
            ::mpfr_set_z(&m_mpfr, integer<1>(n).get_mpz_view(), MPFR_RNDN);
        }
    }
    template <typename T, enable_if_t<conjunction<std::is_integral<T>, std::is_signed<T>>::value, int> = 0>
    void dispatch_construction(const T &n, ::mpfr_prec_t p)
    {
        dispatch_integral_init<T>(p);
        if (n <= std::numeric_limits<long>::max() && n >= std::numeric_limits<long>::min()) {
            ::mpfr_set_si(&m_mpfr, static_cast<long>(n), MPFR_RNDN);
        } else {
            ::mpfr_set_z(&m_mpfr, integer<1>(n).get_mpz_view(), MPFR_RNDN);
        }
    }
    template <std::size_t SSize>
    void dispatch_construction(const integer<SSize> &n, ::mpfr_prec_t p)
    {
        ::mpfr_prec_t prec;
        if (p) {
            prec = check_init_prec(p);
        } else {
            // Infer the precision from the bit size of n.
            const auto ls = n.size();
            // Check that ls * GMP_NUMB_BITS <= max_prec.
            if (mppp_unlikely(ls > static_cast<std::make_unsigned<::mpfr_prec_t>::type>(mpfr_prec_max())
                                       / unsigned(GMP_NUMB_BITS))) {
                throw std::invalid_argument(
                    "The deduced precision for a real constructed from an integer is too large");
            }
            // Compute the precision. We already know it's a non-negative value not greater
            // than the max allowed precision. We just need to make sure it's not smaller than the
            // min allowed precision.
            prec = c_max(static_cast<::mpfr_prec_t>(static_cast<::mpfr_prec_t>(ls) * int(GMP_NUMB_BITS)),
                         mpfr_prec_min());
        }
        ::mpfr_init2(&m_mpfr, prec);
        ::mpfr_set_z(&m_mpfr, n.get_mpz_view(), MPFR_RNDN);
    }
    template <std::size_t SSize>
    void dispatch_construction(const rational<SSize> &q, ::mpfr_prec_t p)
    {
        ::mpfr_prec_t prec;
        if (p) {
            prec = check_init_prec(p);
        } else {
            // Infer the precision from the bit size of num/den.
            const auto n_size = q.get_num().size();
            const auto d_size = q.get_den().size();
            // NOTE: we will check for overflow immediately below, this won't be UB as
            // unsigned arithmetic wraps around.
            const auto tot_size = n_size + d_size;
            // Overflow checks.
            if (mppp_unlikely(
                    // Overflow in total size.
                    (n_size > std::numeric_limits<decltype(q.get_num().size())>::max() - d_size)
                    // Check that tot_size * GMP_NUMB_BITS <= max_prec.
                    || (tot_size > static_cast<std::make_unsigned<::mpfr_prec_t>::type>(mpfr_prec_max())
                                       / unsigned(GMP_NUMB_BITS)))) {
                throw std::invalid_argument(
                    "The deduced precision for a real constructed from a rational is too large");
            }
            // Compute the precision. We already know it's a non-negative value not greater
            // than the max allowed precision. We just need to make sure it's not smaller than the
            // min allowed precision.
            prec = c_max(static_cast<::mpfr_prec_t>(static_cast<::mpfr_prec_t>(tot_size) * int(GMP_NUMB_BITS)),
                         mpfr_prec_min());
        }
        ::mpfr_init2(&m_mpfr, prec);
        ::mpfr_set_q(&m_mpfr, q.get_mpq_view(), MPFR_RNDN);
    }
#if defined(MPPP_WITH_QUADMATH)
    void dispatch_construction(const real128 &x, ::mpfr_prec_t p)
    {
        ::mpfr_prec_t prec;
        if (p) {
            prec = check_init_prec(p);
        } else {
            // The significand precision in bits is 113 for real128.
            assert(real128_sig_digits() == 113u);
            prec = 113;
        }
        // Get the IEEE repr. of x.
        const auto t = x.get_ieee();
        // Assemble the significand.
        integer<2> sig{std::get<2>(t)};
        sig <<= 64u;
        sig += std::get<3>(t);
        // Init the value.
        ::mpfr_init2(&m_mpfr, prec);
        if (std::get<1>(t) == 0u) {
            // Zero or subnormal numbers.
            if (sig.is_zero()) {
                // Zero.
                ::mpfr_set_zero(&m_mpfr, 1);
            } else {
                // Subnormal.
                ::mpfr_set_z(&m_mpfr, sig.get_mpz_view(), MPFR_RNDN);
                ::mpfr_div_2ui(&m_mpfr, &m_mpfr, 16382ul + 112ul, MPFR_RNDN);
            }
        } else if (std::get<1>(t) == 32767u) {
            // NaN or inf.
            if (sig.is_zero()) {
                // inf.
                ::mpfr_set_inf(&m_mpfr, 1);
            } else {
                // NaN.
                ::mpfr_set_nan(&m_mpfr);
            }
        } else {
            // Normal numbers.
            // Set the implicit bit.
            ::mpfr_set_ui_2exp(&m_mpfr, 1ul, static_cast<::mpfr_exp_t>(112), MPFR_RNDN);
            // Set the rest of the significand.
            ::mpfr_add_z(&m_mpfr, &m_mpfr, sig.get_mpz_view(), MPFR_RNDN);
            // Multiply by 2 raised to the adjusted exponent.
            ::mpfr_mul_2si(&m_mpfr, &m_mpfr, static_cast<long>(std::get<1>(t)) - (16383l + 112), MPFR_RNDN);
        }
        if (std::get<0>(t)) {
            // Negate if the sign bit is set.
            ::mpfr_neg(&m_mpfr, &m_mpfr, MPFR_RNDN);
        }
    }
#endif

public:
#if defined(MPPP_HAVE_CONCEPTS)
    explicit real(const RealInteroperable &x,
#else
    template <typename T, real_interoperable_enabler<T> = 0>
    explicit real(const T &x,
#endif
                  ::mpfr_prec_t p = 0)
    {
        dispatch_construction(x, p);
    }
    ~real()
    {
        if (m_mpfr._mpfr_d) {
            ::mpfr_clear(&m_mpfr);
        }
    }
    const mpfr_struct_t *get_mpfr_t() const
    {
        return &m_mpfr;
    }
    ::mpfr_prec_t get_prec() const
    {
        return mpfr_get_prec(&m_mpfr);
    }

private:
    // Utility function to check precision in set_prec().
    static ::mpfr_prec_t check_set_prec(::mpfr_prec_t p)
    {
        if (mppp_unlikely(!mpfr_prec_check(p))) {
            throw std::invalid_argument("Cannot set the precision of a real to the value " + std::to_string(p)
                                        + ": the maximum allowed precision is " + std::to_string(mpfr_prec_max())
                                        + ", the minimum allowed precision is " + std::to_string(mpfr_prec_min()));
        }
        return p;
    }

public:
    real &set_prec(::mpfr_prec_t p)
    {
        ::mpfr_set_prec(&m_mpfr, check_set_prec(p));
        return *this;
    }
    real &set_prec_p(::mpfr_prec_t p)
    {
        if (p != get_prec()) {
            check_set_prec(p);
            // Setup tmp storage with the target precision,
            // and copy this into it.
            MPPP_MAYBE_TLS mpfr_raii mpfr_r(mpfr_prec_min());
            ::mpfr_set_prec(&mpfr_r.m_mpfr, p);
            ::mpfr_set(&mpfr_r.m_mpfr, &m_mpfr, MPFR_RNDN);

            // Set the precision of this to p, and copy back the previous value.
            ::mpfr_set_prec(&m_mpfr, p);
            ::mpfr_set(&m_mpfr, &mpfr_r.m_mpfr, MPFR_RNDN);
        }
        return *this;
    }

private:
    mpfr_struct_t m_mpfr;
};

inline std::ostream &operator<<(std::ostream &os, const real &r)
{
    mpfr_to_stream(r.get_mpfr_t(), os);
    return os;
}
}

#else

#error The real.hpp header was included but mp++ was not configured with the MPPP_WITH_MPFR option.

#endif

#endif