#ifndef CORESTACK_MACRO_UTILITY
#define CORESTACK_MACRO_UTILITY

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

/* Marks a declaration as possibly unused, suppressing -Wunused-function and
 * friends. Must be written before any storage-class specifier, e.g.
 * `MAYBE_UNUSED static int foo(void)`, since that is the only position both the
 * C23 attribute syntax and the GNU attribute syntax accept. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ > 201710L
#define MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
#define MAYBE_UNUSED __attribute__((unused))
#elif defined(_MSC_VER)
#define MAYBE_UNUSED __pragma(warning(suppress : 4505))
#else
#define MAYBE_UNUSED
#endif

/* Marks a function as taking a printf-style format string, enabling -Wformat
 * checking of the variadic arguments against it. `fmt_idx` and `args_idx` are
 * 1-based parameter positions of the format string and of the `...`. Must be
 * written before the declaration, e.g.
 * `FORMAT_PRINTF(1, 2) int log(const char* fmt, ...);`. */
#if defined(__GNUC__) || defined(__clang__)
#define FORMAT_PRINTF(fmt_idx, args_idx) \
    __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define FORMAT_PRINTF(fmt_idx, args_idx)
#endif

#endif
