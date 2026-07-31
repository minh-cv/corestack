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

#endif
