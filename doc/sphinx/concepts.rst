Common concepts
===============

*#include <mp++/concepts.hpp>*

.. note::

   Generic functions and classes in mp++ support `concepts <https://en.wikipedia.org/wiki/Concepts_(C%2B%2B)>`_
   to constrain the types with which they can be used. C++ concepts are not (yet) part of the standard, and they are
   currently available only in GCC 6 and later (with the ``-fconcepts`` compilation flag). When used with compilers which do not
   support concepts natively, mp++ will employ a concept emulation layer in order to provide the same functionality as native
   C++ concepts.

   Since the syntax of native C++ concepts is clearer than that of the concept emulation layer, the mp++ documentation describes
   and refers to concepts in their native C++ form. As long as concepts are not part of the C++ standard, mp++'s concepts
   will be subject to breaking changes, and they should not be considered as part of the public mp++ API.

.. cpp:concept:: template <typename T> mppp::CppInteroperable

   This concept is satisfied by any C++ fundamental type with which the multiprecision classes (such as :cpp:class:`~mppp::integer`,
   :cpp:class:`~mppp::rational`, etc.) can interoperate. The full list of types satisfying this concept is:

   * ``bool``,
   * ``char``, ``signed char`` and ``unsigned char``,
   * ``short`` and ``unsigned short``,
   * ``int`` and ``unsigned``,
   * ``long`` and ``unsigned long``,
   * ``long long`` and ``unsigned long long``,
   * ``float`` and ``double``.

   ``long double`` is also supported, but only if mp++ was configured with the ``MPPP_WITH_MPFR`` option enabled
   (see the :ref:`installation instructions <installation>`).

.. cpp:concept:: template <typename T> mppp::StringType

   This concept is satisfied by C++ string-like types. Specifically, the concept will be true if ``T`` is
   any of the following:

   * ``std::string``,
   * ``const char *``,
   * ``char *``,
   * a ``char`` array of any size.

   Additionally, if at least C++17 is being used, the concept is satisfied also by ``std::string_view``.
