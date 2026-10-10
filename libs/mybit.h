/*
 * Copyright (c) 2026 GeraPro2_0
 *
 * Licence: MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MYBIT_H
#define MYBIT_H

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || defined(__cplusplus) || defined(_MSC_VER)
    #include <stdint.h>
#else
    #if !defined(_STDINT_H) && !defined(_STDINT_H_) && !defined(_GCC_STDINT_H) && !defined(__stdint_h__)
        #define _STDINT_H
        #define _STDINT_H_
        #define _GCC_STDINT_H
        #define __stdint_h__

        typedef unsigned char      uint8_t;
        typedef unsigned short     uint16_t;
        typedef unsigned int       uint32_t;
        #if defined(__GNUC__) || defined(__clang__)
            typedef unsigned long long uint64_t;
        #else
            typedef unsigned __int64   uint64_t;
        #endif
    #endif
#endif

#if defined(_MSC_VER)
    #include <intrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MYBIT_ASSUME_LITTLE_ENDIAN)
    /* Allow explicit override for cross-compilation */
    #if MYBIT_ASSUME_LITTLE_ENDIAN
        #define MYBIT_IS_LITTLE_ENDIAN 1
    #else
        #define MYBIT_IS_LITTLE_ENDIAN 0
    #endif
#elif defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
    #define MYBIT_IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#elif defined(_WIN32) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define MYBIT_IS_LITTLE_ENDIAN 1
#else
    #define MYBIT_IS_LITTLE_ENDIAN 1
#endif

#if defined(_MSC_VER)
    #define MYBIT_INLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define MYBIT_INLINE static inline __attribute__((always_inline))
#else
    #define MYBIT_INLINE static inline
#endif

#include <stddef.h>

/* Feature detection: prefer C11 when available */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define MYBIT_HAS_C11 1
#else
#define MYBIT_HAS_C11 0
#endif

/* Compiler builtin detection helper */
#if defined(__has_builtin)
    #define MYBIT_HAS_BUILTIN(x) __has_builtin(x)
#else
    #define MYBIT_HAS_BUILTIN(x) 0
#endif

/* Portable static assert for pre-C11 compilers */
#define MYBIT_CONCAT(a,b) MYBIT_CONCAT1(a,b)
#define MYBIT_CONCAT1(a,b) a##b
#if MYBIT_HAS_C11
#define MYBIT_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define MYBIT_STATIC_ASSERT(cond, msg) typedef char MYBIT_CONCAT(static_assert_, __LINE__)[(cond) ? 1 : -1]
#endif

/* core byte-swap functions with mybit_ prefix to avoid collisions */
MYBIT_INLINE uint16_t mybit_bswap16(uint16_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap16(x);
#elif defined(_MSC_VER)
    return _byteswap_ushort(x);
#else
    return (uint16_t)((x << 8) | (x >> 8));
#endif
}

MYBIT_INLINE uint32_t mybit_bswap32(uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(x);
#elif defined(_MSC_VER)
    return _byteswap_ulong(x);
#else
    return ((x & 0x000000FF) << 24) |
           ((x & 0x0000FF00) << 8)  |
           ((x & 0x00FF0000) >> 8)  |
           ((x & 0xFF000000) >> 24);
#endif
}

MYBIT_INLINE uint64_t mybit_bswap64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(x);
#elif defined(_MSC_VER)
    return _byteswap_uint64(x);
#else
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 48) |
           ((x & 0x0000000000FF0000ULL) << 40) |
           ((x & 0x00000000FF000000ULL) << 32) |
           ((x & 0x000000FF00000000ULL) >>  8) |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
#endif
}

MYBIT_INLINE uint8_t mybit_bswap8(uint8_t x) {
    return x;
}

/* Provide helpers for unsigned long / unsigned long long so _Generic can
   dispatch on common C typedefs whose width may vary by platform. */
MYBIT_INLINE unsigned long mybit_bswap_ul(unsigned long x) {
    if (sizeof(unsigned long) <= 4)
        return (unsigned long)mybit_bswap32((uint32_t)x);
    return (unsigned long)mybit_bswap64((uint64_t)x);
}

MYBIT_INLINE unsigned long long mybit_bswap_ull(unsigned long long x) {
    if (sizeof(unsigned long long) <= 4)
        return (unsigned long long)mybit_bswap32((uint32_t)x);
    return (unsigned long long)mybit_bswap64((uint64_t)x);
}

#if !defined(MYBIT_BSWAP_SZ_DEFINED)
#define MYBIT_BSWAP_SZ_DEFINED 1
MYBIT_INLINE size_t mybit_bswap_sz(size_t x) {
    return (sizeof(size_t) <= 4) ? (size_t)mybit_bswap32((uint32_t)x) : (size_t)mybit_bswap64((uint64_t)x);
}
#endif

#if MYBIT_HAS_C11 && !defined(__cplusplus)
    #undef mybit_bswap
    #define mybit_bswap(x) _Generic((x), \
        uint8_t: mybit_bswap8((uint8_t)(x)), \
        unsigned char: mybit_bswap8((unsigned char)(x)), \
        signed char: mybit_bswap8((signed char)(x)), \
        char: mybit_bswap8((char)(x)), \
        uint16_t: mybit_bswap16((uint16_t)(x)), \
        short: mybit_bswap16((uint16_t)(x)), \
        unsigned short: mybit_bswap16((uint16_t)(x)), \
        uint32_t: mybit_bswap32((uint32_t)(x)), \
        int: mybit_bswap32((uint32_t)(x)), \
        unsigned int: mybit_bswap32((uint32_t)(x)), \
        uint64_t: mybit_bswap64((uint64_t)(x)), \
        long long: mybit_bswap64((uint64_t)(x)), \
        unsigned long long: mybit_bswap_ull((unsigned long long)(x)), \
        long: mybit_bswap_ul((unsigned long)(x)), \
        unsigned long: mybit_bswap_ul((unsigned long)(x)), \
        size_t: mybit_bswap_sz((size_t)(x)) \
    )
#elif defined(__cplusplus)
    /* In C++ the overloads below provide `mybit_bswap` without macro expansion. */
#else
    /* If no _Generic, provide a safer sizeof-based macro that handles common typedefs */
    MYBIT_INLINE unsigned int mybit_bswap_ui(unsigned int x) {
        return (sizeof(unsigned int) <= 4) ? (unsigned int)mybit_bswap32((uint32_t)x) : (unsigned int)mybit_bswap64((uint64_t)x);
    }
    #if !defined(MYBIT_BSWAP_SZ_DEFINED)
    MYBIT_INLINE size_t mybit_bswap_sz(size_t x) {
        return (sizeof(size_t) <= 4) ? (size_t)mybit_bswap32((uint32_t)x) : (size_t)mybit_bswap64((uint64_t)x);
    }
    #endif

    #undef mybit_bswap
    #define mybit_bswap(x) ( \
        sizeof(x) == 1 ? (x) : \
        sizeof(x) == 2 ? mybit_bswap16((uint16_t)(x)) : \
        sizeof(x) == 4 ? mybit_bswap32((uint32_t)(x)) : \
        sizeof(x) == 8 ? mybit_bswap64((uint64_t)(x)) : \
        sizeof(x) == sizeof(unsigned long) ? mybit_bswap_ul((unsigned long)(x)) : \
        sizeof(x) == sizeof(size_t) ? mybit_bswap_sz((size_t)(x)) : \
        mybit_bswap_ull((unsigned long long)(x)) \
    )
#endif

/* Basic compile-time sanity checks */
#include <limits.h>
/* Sanity checks (use portable MYBIT_STATIC_ASSERT where available) */
MYBIT_STATIC_ASSERT(CHAR_BIT == 8, "mybit.h requires 8-bit bytes");
MYBIT_STATIC_ASSERT(sizeof(uint16_t) == 2, "uint16_t must be 2 bytes");
MYBIT_STATIC_ASSERT(sizeof(uint32_t) == 4, "uint32_t must be 4 bytes");
MYBIT_STATIC_ASSERT(sizeof(uint64_t) == 8, "uint64_t must be 8 bytes");

static inline unsigned mybit_clz32(uint32_t x);
static inline unsigned mybit_ctz32(uint32_t x);
static inline unsigned mybit_clz64(uint64_t x);
static inline unsigned mybit_ctz64(uint64_t x);

/* C wrappers for unsigned long and unsigned long long that dispatch based on
   compile-time size to the proper 32/64-bit implementation. */

MYBIT_INLINE unsigned mybit_countl_one32(uint32_t x) {
    return mybit_clz32(~x);
}

MYBIT_INLINE unsigned mybit_countr_one32(uint32_t x) {
    return mybit_ctz32(~x);
}

MYBIT_INLINE unsigned mybit_countl_one64(uint64_t x) {
    return mybit_clz64(~x);
}

MYBIT_INLINE unsigned mybit_countr_one64(uint64_t x) {
    return mybit_ctz64(~x);
}

MYBIT_INLINE unsigned mybit_countl_one_ul(unsigned long x) {
    if (sizeof(unsigned long) <= 4) return mybit_countl_one32((uint32_t)x);
    return mybit_countl_one64((uint64_t)x);
}

MYBIT_INLINE unsigned mybit_countr_one_ul(unsigned long x) {
    if (sizeof(unsigned long) <= 4) return mybit_countr_one32((uint32_t)x);
    return mybit_countr_one64((uint64_t)x);
}

MYBIT_INLINE unsigned mybit_countl_one_ull(unsigned long long x) {
    if (sizeof(unsigned long long) <= 4) return mybit_countl_one32((uint32_t)x);
    return mybit_countl_one64((uint64_t)x);
}

MYBIT_INLINE unsigned mybit_countr_one_ull(unsigned long long x) {
    if (sizeof(unsigned long long) <= 4) return mybit_countr_one32((uint32_t)x);
    return mybit_countr_one64((uint64_t)x);
}

#if MYBIT_HAS_C11
    #define mybit_countl_one(x) _Generic((x), \
        uint8_t: mybit_countl_one32((uint32_t)(x)), \
        uint16_t: mybit_countl_one32((uint32_t)(x)), \
        uint32_t: mybit_countl_one32((uint32_t)(x)), \
        uint64_t: mybit_countl_one64((uint64_t)(x)), \
        default: mybit_countl_one_ull((unsigned long long)(x)) \
    )

    #define mybit_countr_one(x) _Generic((x), \
        uint8_t: mybit_countr_one32((uint32_t)(x)), \
        uint16_t: mybit_countr_one32((uint32_t)(x)), \
        uint32_t: mybit_countr_one32((uint32_t)(x)), \
        uint64_t: mybit_countr_one64((uint64_t)(x)), \
        default: mybit_countr_one64((uint64_t)(x)) \
    )

    #define mybit_has_single_bit(x) _Generic((x), \
        uint8_t: mybit_has_single_bit8((uint8_t)(x)), \
        uint16_t: mybit_has_single_bit16((uint16_t)(x)), \
        uint32_t: mybit_has_single_bit32((uint32_t)(x)), \
        uint64_t: mybit_has_single_bit64((uint64_t)(x)), \
        default: mybit_has_single_bit64((uint64_t)(x)) \
    )

    #define mybit_bit_floor(x) _Generic((x), \
        uint8_t: mybit_bit_floor8((uint8_t)(x)), \
        uint16_t: mybit_bit_floor16((uint16_t)(x)), \
        uint32_t: mybit_bit_floor32((uint32_t)(x)), \
        uint64_t: mybit_bit_floor64((uint64_t)(x)), \
        default: mybit_bit_floor64((uint64_t)(x)) \
    )

    #define mybit_bit_ceil(x) _Generic((x), \
        uint8_t: mybit_bit_ceil8((uint8_t)(x)), \
        uint16_t: mybit_bit_ceil16((uint16_t)(x)), \
        uint32_t: mybit_bit_ceil32((uint32_t)(x)), \
        uint64_t: mybit_bit_ceil64((uint64_t)(x)), \
        default: mybit_bit_ceil64((uint64_t)(x)) \
    )
#else
    /* Fallback for pre-C11: use sizeof-based macro dispatch */
    MYBIT_INLINE unsigned mybit_countl_one_ui(unsigned int x) {
        return (sizeof(unsigned int) <= 4) ? mybit_countl_one32((uint32_t)x) : mybit_countl_one64((uint64_t)x);
    }
    MYBIT_INLINE unsigned mybit_countl_one_sz(size_t x) {
        return (sizeof(size_t) <= 4) ? mybit_countl_one32((uint32_t)x) : mybit_countl_one64((uint64_t)x);
    }
    MYBIT_INLINE unsigned mybit_countr_one_ui(unsigned int x) {
        return (sizeof(unsigned int) <= 4) ? mybit_countr_one32((uint32_t)x) : mybit_countr_one64((uint64_t)x);
    }
    MYBIT_INLINE unsigned mybit_countr_one_sz(size_t x) {
        return (sizeof(size_t) <= 4) ? mybit_countr_one32((uint32_t)x) : mybit_countr_one64((uint64_t)x);
    }

    #define mybit_countl_one(x) ( \
        sizeof(x) <= 2 ? mybit_countl_one32((uint32_t)(x)) : \
        sizeof(x) == 4 ? mybit_countl_one32((uint32_t)(x)) : \
        sizeof(x) == 8 ? mybit_countl_one64((uint64_t)(x)) : \
        sizeof(x) == sizeof(unsigned long) ? mybit_countl_one_ul((unsigned long)(x)) : \
        sizeof(x) == sizeof(size_t) ? mybit_countl_one_sz((size_t)(x)) : \
        mybit_countl_one_ull((unsigned long long)(x)) \
    )

    #define mybit_countr_one(x) ( \
        sizeof(x) <= 2 ? mybit_countr_one32((uint32_t)(x)) : \
        sizeof(x) == 4 ? mybit_countr_one32((uint32_t)(x)) : \
        sizeof(x) == 8 ? mybit_countr_one64((uint64_t)(x)) : \
        sizeof(x) == sizeof(unsigned long) ? mybit_countr_one_ul((unsigned long)(x)) : \
        sizeof(x) == sizeof(size_t) ? mybit_countr_one_sz((size_t)(x)) : \
        mybit_countr_one_ull((unsigned long long)(x)) \
    )
#endif

/* static assertions to ensure expected type sizes and 8-bit char */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    _Static_assert(sizeof(uint16_t) == 2, "uint16_t must be 2 bytes");
    _Static_assert(sizeof(uint32_t) == 4, "uint32_t must be 4 bytes");
    _Static_assert(sizeof(uint64_t) == 8, "uint64_t must be 8 bytes");
#endif

/* portable bit operations */

MYBIT_INLINE unsigned mybit_clz32(uint32_t x) {
    if (x == 0u) return 32u;
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_clz(x);
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanReverse(&index, x);
    return 31u - (unsigned)index;
#else
    unsigned n = 0u;
    if ((x & 0xFFFF0000u) == 0u) { n += 16u; x <<= 16; }
    if ((x & 0xFF000000u) == 0u) { n += 8u; x <<= 8; }
    if ((x & 0xF0000000u) == 0u) { n += 4u; x <<= 4; }
    if ((x & 0xC0000000u) == 0u) { n += 2u; x <<= 2; }
    if ((x & 0x80000000u) == 0u) { n += 1u; }
    return n;
#endif
}

MYBIT_INLINE unsigned mybit_ctz32(uint32_t x) {
    if (x == 0u) return 32u;
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctz(x);
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, x);
    return (unsigned)index;
#else
    unsigned n = 0u;
    if ((x & 0x0000FFFFu) == 0u) { n += 16u; x >>= 16; }
    if ((x & 0x000000FFu) == 0u) { n += 8u; x >>= 8; }
    if ((x & 0x0000000Fu) == 0u) { n += 4u; x >>= 4; }
    if ((x & 0x00000003u) == 0u) { n += 2u; x >>= 2; }
    if ((x & 0x00000001u) == 0u) { n += 1u; }
    return n;
#endif
}

MYBIT_INLINE unsigned mybit_popcount32(uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_popcount(x);
#elif defined(_MSC_VER)
    return (unsigned)__popcnt(x);
#else
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = ((x + (x >> 4)) & 0x0F0F0F0Fu);
    return (unsigned)((x * 0x01010101u) >> 24);
#endif
}

MYBIT_INLINE uint32_t mybit_rotl32(uint32_t x, unsigned r) {
#if defined(_MSC_VER)
    return _rotl(x, (int)r);
#else
    return (x << (r & 31u)) | (x >> ((32u - r) & 31u));
#endif
}

MYBIT_INLINE uint32_t mybit_rotr32(uint32_t x, unsigned r) {
#if defined(_MSC_VER)
    return _rotr(x, (int)r);
#else
    return (x >> (r & 31u)) | (x << ((32u - r) & 31u));
#endif
}

MYBIT_INLINE uint32_t mybit_reverse32(uint32_t x) {
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}

MYBIT_INLINE unsigned mybit_clz64(uint64_t x) {
    if (x == 0ULL) return 64u;
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_clzll(x);
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanReverse64(&index, x);
    return 63u - (unsigned)index;
#else
    unsigned n = 0u;
    if ((x >> 32) == 0ULL) { n += 32u; x <<= 32; }
    if ((x >> 48) == 0ULL) { n += 16u; x <<= 16; }
    if ((x >> 56) == 0ULL) { n += 8u; x <<= 8; }
    if ((x >> 60) == 0ULL) { n += 4u; x <<= 4; }
    if ((x >> 62) == 0ULL) { n += 2u; x <<= 2; }
    if ((x >> 63) == 0ULL) { n += 1u; }
    return n;
#endif
}

MYBIT_INLINE unsigned mybit_ctz64(uint64_t x) {
    if (x == 0ULL) return 64u;
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(x);
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanForward64(&index, x);
    return (unsigned)index;
#else
    unsigned n = 0u;
    if ((x & 0xFFFFFFFFULL) == 0ULL) { n += 32u; x >>= 32; }
    if ((x & 0x0000FFFFULL) == 0ULL) { n += 16u; x >>= 16; }
    if ((x & 0x000000FFULL) == 0ULL) { n += 8u; x >>= 8; }
    if ((x & 0x0000000FULL) == 0ULL) { n += 4u; x >>= 4; }
    if ((x & 0x00000003ULL) == 0ULL) { n += 2u; x >>= 2; }
    if ((x & 0x00000001ULL) == 0ULL) { n += 1u; }
    return n;
#endif
}

MYBIT_INLINE unsigned mybit_popcount64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_popcountll(x);
#elif defined(_MSC_VER)
    return (unsigned)__popcnt64(x);
#else
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = ((x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL);
    return (unsigned)((x * 0x0101010101010101ULL) >> 56);
#endif
}

MYBIT_INLINE uint64_t mybit_rotl64(uint64_t x, unsigned r) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    return _rotl64(x, (int)r);
#else
    return (x << (r & 63u)) | (x >> ((64u - r) & 63u));
#endif
}

MYBIT_INLINE uint64_t mybit_rotr64(uint64_t x, unsigned r) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    return _rotr64(x, (int)r);
#else
    return (x >> (r & 63u)) | (x << ((64u - r) & 63u));
#endif
}

MYBIT_INLINE uint64_t mybit_reverse64(uint64_t x) {
    x = ((x >> 1) & 0x5555555555555555ULL) | ((x & 0x5555555555555555ULL) << 1);
    x = ((x >> 2) & 0x3333333333333333ULL) | ((x & 0x3333333333333333ULL) << 2);
    x = ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL) | ((x & 0x0F0F0F0F0F0F0F0FULL) << 4);
    x = ((x >> 8) & 0x00FF00FF00FF00FFULL) | ((x & 0x00FF00FF00FF00FFULL) << 8);
    x = ((x >> 16) & 0x0000FFFF0000FFFFULL) | ((x & 0x0000FFFF0000FFFFULL) << 16);
    return (x >> 32) | (x << 32);
}

MYBIT_INLINE unsigned mybit_clz16(uint16_t x) {
    if (x == 0u) return 16u;
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_clz((uint32_t)x) - 16u;
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanReverse(&index, x);
    return 15u - (unsigned)index;
#else
    unsigned n = 0u;
    if ((x & 0xFF00u) == 0u) { n += 8u; x <<= 8; }
    if ((x & 0xF000u) == 0u) { n += 4u; x <<= 4; }
    if ((x & 0xC000u) == 0u) { n += 2u; x <<= 2; }
    if ((x & 0x8000u) == 0u) { n += 1u; }
    return n;
#endif
}

MYBIT_INLINE unsigned mybit_ctz16(uint16_t x) {
    if (x == 0u) return 16u;
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctz((uint32_t)x);
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, x);
    return (unsigned)index;
#else
    unsigned n = 0u;
    if ((x & 0x00FFu) == 0u) { n += 8u; x >>= 8; }
    if ((x & 0x000Fu) == 0u) { n += 4u; x >>= 4; }
    if ((x & 0x0003u) == 0u) { n += 2u; x >>= 2; }
    if ((x & 0x0001u) == 0u) { n += 1u; }
    return n;
#endif
}

MYBIT_INLINE unsigned mybit_popcount16(uint16_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_popcount((uint32_t)x);
#elif defined(_MSC_VER)
    return (unsigned)__popcnt((unsigned)x);
#else
    x = x - ((x >> 1) & 0x5555u);
    x = (x & 0x3333u) + ((x >> 2) & 0x3333u);
    x = ((x + (x >> 4)) & 0x0F0Fu);
    return (unsigned)((x * 0x0101u) >> 8);
#endif
}

MYBIT_INLINE uint16_t mybit_rotl16(uint16_t x, unsigned r) {
    return (uint16_t)((x << (r & 15u)) | (x >> ((16u - r) & 15u)));
}

MYBIT_INLINE uint16_t mybit_rotr16(uint16_t x, unsigned r) {
    return (uint16_t)((x >> (r & 15u)) | (x << ((16u - r) & 15u)));
}

MYBIT_INLINE uint16_t mybit_reverse16(uint16_t x) {
    x = (uint16_t)(((x >> 1) & 0x5555u) | ((x & 0x5555u) << 1));
    x = (uint16_t)(((x >> 2) & 0x3333u) | ((x & 0x3333u) << 2));
    return (uint16_t)(((x >> 4) & 0x0F0Fu) | ((x & 0x0F0Fu) << 4));
}

MYBIT_INLINE unsigned mybit_clz8(uint8_t x) {
    if (x == 0u) return 8u;
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_clz((uint32_t)x) - 24u;
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanReverse(&index, x);
    return 7u - (unsigned)index;
#else
    unsigned n = 0u;
    if ((x & 0xF0u) == 0u) { n += 4u; x <<= 4; }
    if ((x & 0xC0u) == 0u) { n += 2u; x <<= 2; }
    if ((x & 0x80u) == 0u) { n += 1u; }
    return n;
#endif
}

MYBIT_INLINE unsigned mybit_ctz8(uint8_t x) {
    if (x == 0u) return 8u;
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctz((uint32_t)x);
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, x);
    return (unsigned)index;
#else
    unsigned n = 0u;
    if ((x & 0x0Fu) == 0u) { n += 4u; x >>= 4; }
    if ((x & 0x03u) == 0u) { n += 2u; x >>= 2; }
    if ((x & 0x01u) == 0u) { n += 1u; }
    return n;
#endif
}

MYBIT_INLINE unsigned mybit_popcount8(uint8_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_popcount((unsigned)x);
#elif defined(_MSC_VER)
    return (unsigned)__popcnt((unsigned)x);
#else
    x = x - ((x >> 1) & 0x55u);
    x = (uint8_t)((x & 0x33u) + ((x >> 2) & 0x33u));
    return (unsigned)((x * 0x11u) >> 4);
#endif
}

MYBIT_INLINE uint8_t mybit_rotl8(uint8_t x, unsigned r) {
    return (uint8_t)((x << (r & 7u)) | (x >> ((8u - r) & 7u)));
}

MYBIT_INLINE uint8_t mybit_rotr8(uint8_t x, unsigned r) {
    return (uint8_t)((x >> (r & 7u)) | (x << ((8u - r) & 7u)));
}

MYBIT_INLINE uint8_t mybit_reverse8(uint8_t x) {
    x = (uint8_t)(((x >> 1) & 0x55u) | ((x & 0x55u) << 1));
    x = (uint8_t)(((x >> 2) & 0x33u) | ((x & 0x33u) << 2));
    return (uint8_t)(((x >> 4) & 0x0Fu) | ((x & 0x0Fu) << 4));
}

MYBIT_INLINE unsigned mybit_has_single_bit16(uint16_t x) {
    return x != 0u && (x & (x - 1u)) == 0u;
}

MYBIT_INLINE unsigned mybit_has_single_bit8(uint8_t x) {
    return x != 0u && (x & (x - 1u)) == 0u;
}

MYBIT_INLINE uint16_t mybit_bit_floor16(uint16_t x) {
    return x == 0u ? 0u : (uint16_t)(1u << (15u - mybit_clz16(x)));
}

MYBIT_INLINE uint8_t mybit_bit_floor8(uint8_t x) {
    return x == 0u ? 0u : (uint8_t)(1u << (7u - mybit_clz8(x)));
}

MYBIT_INLINE uint16_t mybit_bit_ceil16(uint16_t x) {
    if (x == 0u) return 0u;
    x -= 1u;
    x |= x >> 1u;
    x |= x >> 2u;
    x |= x >> 4u;
    x |= x >> 8u;
    return x + 1u;
}

MYBIT_INLINE uint8_t mybit_bit_ceil8(uint8_t x) {
    if (x == 0u) return 0u;
    x -= 1u;
    x |= x >> 1u;
    x |= x >> 2u;
    x |= x >> 4u;
    return x + 1u;
}

MYBIT_INLINE unsigned mybit_has_single_bit32(uint32_t x) {
    return x != 0u && (x & (x - 1u)) == 0u;
}

MYBIT_INLINE unsigned mybit_has_single_bit64(uint64_t x) {
    return x != 0ULL && (x & (x - 1ULL)) == 0ULL;
}

MYBIT_INLINE uint32_t mybit_bit_floor32(uint32_t x) {
    return x == 0u ? 0u : (1u << (31u - mybit_clz32(x)));
}

MYBIT_INLINE uint64_t mybit_bit_floor64(uint64_t x) {
    return x == 0ULL ? 0ULL : (1ULL << (63u - mybit_clz64(x)));
}

MYBIT_INLINE uint32_t mybit_bit_ceil32(uint32_t x) {
    if (x == 0u) return 0u;
    x -= 1u;
    x |= x >> 1u;
    x |= x >> 2u;
    x |= x >> 4u;
    x |= x >> 8u;
    x |= x >> 16u;
    return x + 1u;
}

MYBIT_INLINE uint64_t mybit_bit_ceil64(uint64_t x) {
    if (x == 0ULL) return 0ULL;
    x -= 1ULL;
    x |= x >> 1ULL;
    x |= x >> 2ULL;
    x |= x >> 4ULL;
    x |= x >> 8ULL;
    x |= x >> 16ULL;
    x |= x >> 32ULL;
    return x + 1ULL;
}

MYBIT_INLINE uint8_t mybit_load_le8(const void *ptr) {
    return *(const uint8_t *)ptr;
}

MYBIT_INLINE uint8_t mybit_load_be8(const void *ptr) {
    return *(const uint8_t *)ptr;
}

MYBIT_INLINE void mybit_store_le8(void *ptr, uint8_t v) {
    ((uint8_t *)ptr)[0] = v;
}

MYBIT_INLINE void mybit_store_be8(void *ptr, uint8_t v) {
    ((uint8_t *)ptr)[0] = v;
}

MYBIT_INLINE uint16_t mybit_load_le16(const void *ptr) {
    const uint8_t *b = (const uint8_t *)ptr;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

MYBIT_INLINE uint16_t mybit_load_be16(const void *ptr) {
    const uint8_t *b = (const uint8_t *)ptr;
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

MYBIT_INLINE uint32_t mybit_load_le32(const void *ptr) {
    const uint8_t *b = (const uint8_t *)ptr;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

MYBIT_INLINE uint32_t mybit_load_be32(const void *ptr) {
    const uint8_t *b = (const uint8_t *)ptr;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

MYBIT_INLINE uint64_t mybit_load_le64(const void *ptr) {
    const uint8_t *b = (const uint8_t *)ptr;
    return (uint64_t)b[0] | ((uint64_t)b[1] << 8) | ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 24) |
           ((uint64_t)b[4] << 32) | ((uint64_t)b[5] << 40) | ((uint64_t)b[6] << 48) | ((uint64_t)b[7] << 56);
}

MYBIT_INLINE uint64_t mybit_load_be64(const void *ptr) {
    const uint8_t *b = (const uint8_t *)ptr;
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) | ((uint64_t)b[6] << 8)  | (uint64_t)b[7];
}

MYBIT_INLINE void mybit_store_le16(void *ptr, uint16_t v) {
    uint8_t *b = (uint8_t *)ptr;
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
}

MYBIT_INLINE void mybit_store_be16(void *ptr, uint16_t v) {
    uint8_t *b = (uint8_t *)ptr;
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)v;
}

MYBIT_INLINE void mybit_store_le32(void *ptr, uint32_t v) {
    uint8_t *b = (uint8_t *)ptr;
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

MYBIT_INLINE void mybit_store_be32(void *ptr, uint32_t v) {
    uint8_t *b = (uint8_t *)ptr;
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}

MYBIT_INLINE void mybit_store_le64(void *ptr, uint64_t v) {
    uint8_t *b = (uint8_t *)ptr;
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    b[4] = (uint8_t)(v >> 32);
    b[5] = (uint8_t)(v >> 40);
    b[6] = (uint8_t)(v >> 48);
    b[7] = (uint8_t)(v >> 56);
}

MYBIT_INLINE void mybit_store_be64(void *ptr, uint64_t v) {
    uint8_t *b = (uint8_t *)ptr;
    b[0] = (uint8_t)(v >> 56);
    b[1] = (uint8_t)(v >> 48);
    b[2] = (uint8_t)(v >> 40);
    b[3] = (uint8_t)(v >> 32);
    b[4] = (uint8_t)(v >> 24);
    b[5] = (uint8_t)(v >> 16);
    b[6] = (uint8_t)(v >> 8);
    b[7] = (uint8_t)v;
}

#if MYBIT_IS_LITTLE_ENDIAN
    #define littleswap16(x) (x)
    #define littleswap32(x) (x)
    #define littleswap64(x) (x)
    #define littleswap(x)   (x)

    #define bigswap16(x) mybit_bswap16(x)
    #define bigswap32(x) mybit_bswap32(x)
    #define bigswap64(x) mybit_bswap64(x)
    #define bigswap(x)   mybit_bswap(x)
#else
    #define littleswap16(x) mybit_bswap16(x)
    #define littleswap32(x) mybit_bswap32(x)
    #define littleswap64(x) mybit_bswap64(x)
    #define littleswap(x)   mybit_bswap(x)

    #define bigswap16(x) (x)
    #define bigswap32(x) (x)
    #define bigswap64(x) (x)
    #define bigswap(x)   (x)
#endif

/* Advanced bit manipulation operations (BMI1/BMI2 extensions) */
MYBIT_INLINE uint32_t mybit_blsr32(uint32_t x) {
#if defined(__BMI__) || defined(__x86_64__) || defined(_M_X64)
    #if defined(__GNUC__) || defined(__clang__)
        #if MYBIT_HAS_BUILTIN(__builtin_ia32_blsr_u32)
            return __builtin_ia32_blsr_u32(x);
        #else
            return x & (x - 1u);
        #endif
    #elif defined(_MSC_VER) && defined(_M_X64)
        return _blsr_u32(x);
    #else
        return x & (x - 1u);
    #endif
#else
    return x & (x - 1u);
#endif
}

MYBIT_INLINE uint64_t mybit_blsr64(uint64_t x) {
#if defined(__BMI__) || defined(__x86_64__) || defined(_M_X64)
    #if defined(__GNUC__) || defined(__clang__)
        #if MYBIT_HAS_BUILTIN(__builtin_ia32_blsr_u64)
            return __builtin_ia32_blsr_u64(x);
        #else
            return x & (x - 1ULL);
        #endif
    #elif defined(_MSC_VER) && defined(_M_X64)
        return _blsr_u64(x);
    #else
        return x & (x - 1ULL);
    #endif
#else
    return x & (x - 1ULL);
#endif
}

MYBIT_INLINE uint32_t mybit_bextr32(uint32_t x, unsigned start, unsigned len) {
#if defined(__BMI2__)
    #if defined(__GNUC__) || defined(__clang__)
        #if MYBIT_HAS_BUILTIN(__builtin_ia32_bextr_u32)
            return __builtin_ia32_bextr_u32(x, (start & 0xFFu) | ((len & 0xFFu) << 8));
        #else
            if (len == 0u) return 0u;
            uint32_t mask = (len >= 32u) ? 0xFFFFFFFFu : ((1u << len) - 1u);
            return (start >= 32u) ? 0u : ((x >> start) & mask);
        #endif
    #elif defined(_MSC_VER)
        return _bextr_u32(x, start, len);
    #else
        if (len == 0u) return 0u;
        uint32_t mask = (len >= 32u) ? 0xFFFFFFFFu : ((1u << len) - 1u);
        return (start >= 32u) ? 0u : ((x >> start) & mask);
    #endif
#else
    if (len == 0u) return 0u;
    uint32_t mask = (len >= 32u) ? 0xFFFFFFFFu : ((1u << len) - 1u);
    return (start >= 32u) ? 0u : ((x >> start) & mask);
#endif
}

MYBIT_INLINE uint64_t mybit_bextr64(uint64_t x, unsigned start, unsigned len) {
#if defined(__BMI2__) && (defined(__x86_64__) || defined(_M_X64))
    #if defined(__GNUC__) || defined(__clang__)
        #if MYBIT_HAS_BUILTIN(__builtin_ia32_bextr_u64)
            return __builtin_ia32_bextr_u64(x, (start & 0xFFu) | ((len & 0xFFu) << 8));
        #else
            if (len == 0u) return 0u;
            uint64_t mask = (len >= 64u) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << len) - 1ULL);
            return (start >= 64u) ? 0u : ((x >> start) & mask);
        #endif
    #elif defined(_MSC_VER)
        return _bextr_u64(x, start, len);
    #else
        if (len == 0u) return 0u;
        uint64_t mask = (len >= 64u) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << len) - 1ULL);
        return (start >= 64u) ? 0u : ((x >> start) & mask);
    #endif
#else
    if (len == 0u) return 0u;
    uint64_t mask = (len >= 64u) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << len) - 1ULL);
    return (start >= 64u) ? 0u : ((x >> start) & mask);
#endif
}

MYBIT_INLINE uint32_t mybit_pext32(uint32_t x, uint32_t mask) {
#if defined(__BMI2__)
    #if defined(__GNUC__) || defined(__clang__)
        #if MYBIT_HAS_BUILTIN(__builtin_ia32_pext_u32)
            return __builtin_ia32_pext_u32(x, mask);
        #else
            uint32_t res = 0;
            for (uint32_t bb = 1; mask; mask &= mask - 1, bb <<= 1) {
                if (x & mask & -(int32_t)mask) res |= bb;
            }
            return res;
        #endif
    #elif defined(_MSC_VER)
        return _pext_u32(x, mask);
    #else
        uint32_t res = 0;
        for (uint32_t bb = 1; mask; mask &= mask - 1, bb <<= 1) {
            if (x & mask & -(int32_t)mask) res |= bb;
        }
        return res;
    #endif
#else
    uint32_t res = 0;
    for (uint32_t bb = 1; mask; mask &= mask - 1, bb <<= 1) {
        if (x & mask & -(int32_t)mask) res |= bb;
    }
    return res;
#endif
}

MYBIT_INLINE uint64_t mybit_pext64(uint64_t x, uint64_t mask) {
#if defined(__BMI2__) && (defined(__x86_64__) || defined(_M_X64))
    #if defined(__GNUC__) || defined(__clang__)
        #if MYBIT_HAS_BUILTIN(__builtin_ia32_pext_u64)
            return __builtin_ia32_pext_u64(x, mask);
        #else
            uint64_t res = 0;
            for (uint64_t bb = 1; mask; mask &= mask - 1, bb <<= 1) {
                if (x & mask & -(int64_t)mask) res |= bb;
            }
            return res;
        #endif
    #elif defined(_MSC_VER)
        return _pext_u64(x, mask);
    #else
        uint64_t res = 0;
        for (uint64_t bb = 1; mask; mask &= mask - 1, bb <<= 1) {
            if (x & mask & -(int64_t)mask) res |= bb;
        }
        return res;
    #endif
#else
    uint64_t res = 0;
    for (uint64_t bb = 1; mask; mask &= mask - 1, bb <<= 1) {
        if (x & mask & -(int64_t)mask) res |= bb;
    }
    return res;
#endif
}

MYBIT_INLINE uint32_t mybit_pdep32(uint32_t x, uint32_t mask) {
#if defined(__BMI2__)
    #if defined(__GNUC__) || defined(__clang__)
        #if MYBIT_HAS_BUILTIN(__builtin_ia32_pdep_u32)
            return __builtin_ia32_pdep_u32(x, mask);
        #else
            uint32_t res = 0;
            while (mask) {
                uint32_t m = mask & -(int32_t)mask;
                if (x & 1) res |= m;
                x >>= 1;
                mask &= mask - 1;
            }
            return res;
        #endif
    #elif defined(_MSC_VER)
        return _pdep_u32(x, mask);
    #else
        uint32_t res = 0;
        while (mask) {
            uint32_t m = mask & -(int32_t)mask;
            if (x & 1) res |= m;
            x >>= 1;
            mask &= mask - 1;
        }
        return res;
    #endif
#else
    uint32_t res = 0;
    while (mask) {
        uint32_t m = mask & -(int32_t)mask;
        if (x & 1) res |= m;
        x >>= 1;
        mask &= mask - 1;
    }
    return res;
#endif
}

MYBIT_INLINE uint64_t mybit_pdep64(uint64_t x, uint64_t mask) {
#if defined(__BMI2__) && (defined(__x86_64__) || defined(_M_X64))
    #if defined(__GNUC__) || defined(__clang__)
        #if MYBIT_HAS_BUILTIN(__builtin_ia32_pdep_u64)
            return __builtin_ia32_pdep_u64(x, mask);
        #else
            uint64_t res = 0;
            while (mask) {
                uint64_t m = mask & -(int64_t)mask;
                if (x & 1) res |= m;
                x >>= 1;
                mask &= mask - 1;
            }
            return res;
        #endif
    #elif defined(_MSC_VER)
        return _pdep_u64(x, mask);
    #else
        uint64_t res = 0;
        while (mask) {
            uint64_t m = mask & -(int64_t)mask;
            if (x & 1) res |= m;
            x >>= 1;
            mask &= mask - 1;
        }
        return res;
    #endif
#else
    uint64_t res = 0;
    while (mask) {
        uint64_t m = mask & -(int64_t)mask;
        if (x & 1) res |= m;
        x >>= 1;
        mask &= mask - 1;
    }
    return res;
#endif
}

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
inline uint8_t mybit_bswap(uint8_t x) { return x; }
inline uint16_t mybit_bswap(uint16_t x) { return mybit_bswap16(x); }
inline uint32_t mybit_bswap(uint32_t x) { return mybit_bswap32(x); }
inline uint64_t mybit_bswap(uint64_t x) { return mybit_bswap64(x); }

/* Helpful constexpr templates for C++: usable at compile-time when possible. */
#include <type_traits>

namespace mybit {
    template<typename T>
    using _enable_unsigned = typename std::enable_if<std::is_unsigned<T>::value>::type;

    template<typename T, typename = _enable_unsigned<T>>
    constexpr bool has_single_bit(T x) noexcept { return x != 0 && (x & (x - 1)) == 0; }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr unsigned countl_zero(T x) noexcept {
#if defined(__cpp_lib_is_constant_evaluated) || (defined(__cplusplus) && __cplusplus >= 202002L)
        if (std::is_constant_evaluated()) {
            const unsigned bits = sizeof(T) * 8u;
            unsigned n = 0u;
            for (unsigned i = 0u; i < bits; ++i) {
                if ((x & (T(1) << (bits - 1u - i))) == 0) n++; else break;
            }
            return n;
        }
#endif
        if (x == 0) return sizeof(T) * 8u;
        if (sizeof(T) <= 4u) {
            return mybit_clz32(static_cast<uint32_t>(x)) - (32u - (sizeof(T) * 8u));
        } else {
            return mybit_clz64(static_cast<uint64_t>(x));
        }
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr unsigned countl_one(T x) noexcept { return countl_zero(static_cast<T>(~x)); }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr unsigned countr_zero(T x) noexcept {
#if defined(__cpp_lib_is_constant_evaluated) || (defined(__cplusplus) && __cplusplus >= 202002L)
        if (std::is_constant_evaluated()) {
            const unsigned bits = sizeof(T) * 8u;
            unsigned n = 0u;
            for (unsigned i = 0u; i < bits; ++i) {
                if ((x & (T(1) << i)) == 0) n++; else break;
            }
            return n;
        }
#endif
        if (sizeof(T) <= 4u) {
            return mybit_ctz32(static_cast<uint32_t>(x));
        } else {
            return mybit_ctz64(static_cast<uint64_t>(x));
        }
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr unsigned countr_one(T x) noexcept { return countr_zero(static_cast<T>(~x)); }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T bit_floor(T x) noexcept {
        if (x == 0u) return 0u;
        return T(1) << (static_cast<unsigned>((sizeof(T) * 8u) - 1u) - countl_zero(x));
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T bit_ceil(T x) noexcept {
        if (x <= 1u) return 1u;
        return T(1) << (static_cast<unsigned>(sizeof(T) * 8u) - countl_zero(static_cast<T>(x - 1u)));
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T rotl(T x, unsigned r) noexcept {
        const unsigned bits = sizeof(T) * 8u;
        r &= (bits - 1u);
        return (x << r) | (x >> ((bits - r) & (bits - 1u)));
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T rotr(T x, unsigned r) noexcept {
        const unsigned bits = sizeof(T) * 8u;
        r &= (bits - 1u);
        return (x >> r) | (x << ((bits - r) & (bits - 1u)));
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr unsigned popcount(T x) noexcept {
#if defined(__cpp_lib_is_constant_evaluated) || (defined(__cplusplus) && __cplusplus >= 202002L)
        if (std::is_constant_evaluated()) {
            unsigned n = 0u;
            while (x) { n += unsigned(x & T(1)); x >>= 1u; }
            return n;
        }
#endif
        if (sizeof(T) <= 4u) return mybit_popcount32(static_cast<uint32_t>(x));
        return mybit_popcount64(static_cast<uint64_t>(x));
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T byteswap(T x) noexcept {
        if (sizeof(T) == 1u) return x;
        if (sizeof(T) == 2u) return static_cast<T>(mybit_bswap16(static_cast<uint16_t>(x)));
        if (sizeof(T) == 4u) return static_cast<T>(mybit_bswap32(static_cast<uint32_t>(x)));
        return static_cast<T>(mybit_bswap64(static_cast<uint64_t>(x)));
    }

    template<typename T, typename = _enable_unsigned<T>>
    MYBIT_INLINE T load_le(const void *ptr) noexcept {
        if (sizeof(T) == 1u) return static_cast<T>(mybit_load_le8(ptr));
        if (sizeof(T) == 2u) return static_cast<T>(mybit_load_le16(ptr));
        if (sizeof(T) == 4u) return static_cast<T>(mybit_load_le32(ptr));
        return static_cast<T>(mybit_load_le64(ptr));
    }

    template<typename T, typename = _enable_unsigned<T>>
    MYBIT_INLINE T load_be(const void *ptr) noexcept {
        if (sizeof(T) == 1u) return static_cast<T>(mybit_load_be8(ptr));
        if (sizeof(T) == 2u) return static_cast<T>(mybit_load_be16(ptr));
        if (sizeof(T) == 4u) return static_cast<T>(mybit_load_be32(ptr));
        return static_cast<T>(mybit_load_be64(ptr));
    }

    template<typename T, typename = _enable_unsigned<T>>
    MYBIT_INLINE void store_le(void *ptr, T v) noexcept {
        if (sizeof(T) == 1u) {
            mybit_store_le8(ptr, static_cast<uint8_t>(v));
        } else if (sizeof(T) == 2u) {
            mybit_store_le16(ptr, static_cast<uint16_t>(v));
        } else if (sizeof(T) == 4u) {
            mybit_store_le32(ptr, static_cast<uint32_t>(v));
        } else {
            mybit_store_le64(ptr, static_cast<uint64_t>(v));
        }
    }

    template<typename T, typename = _enable_unsigned<T>>
    MYBIT_INLINE void store_be(void *ptr, T v) noexcept {
        if (sizeof(T) == 1u) {
            mybit_store_be8(ptr, static_cast<uint8_t>(v));
        } else if (sizeof(T) == 2u) {
            mybit_store_be16(ptr, static_cast<uint16_t>(v));
        } else if (sizeof(T) == 4u) {
            mybit_store_be32(ptr, static_cast<uint32_t>(v));
        } else {
            mybit_store_be64(ptr, static_cast<uint64_t>(v));
        }
    }

    enum class endian {
        little = 0,
        big    = 1,
        native = MYBIT_IS_LITTLE_ENDIAN ? little : big
    };

    template <typename To, typename From>

    MYBIT_INLINE To bit_cast(const From& from) noexcept {
#if defined(__cpp_static_assert) || (defined(__cplusplus) && __cplusplus >= 201103L)
        static_assert(sizeof(To) == sizeof(From), "bit_cast requiere tipos del mismo tamano");
#endif
        To to;
        char* d = reinterpret_cast<char*>(&to);
        const char* s = reinterpret_cast<const char*>(&from);
        for (size_t i = 0; i < sizeof(From); ++i) *d++ = *s++;
        return to;
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T blsr(T x) noexcept {
        if (sizeof(T) <= 4u) return static_cast<T>(mybit_blsr32(static_cast<uint32_t>(x)));
        return static_cast<T>(mybit_blsr64(static_cast<uint64_t>(x)));
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T bextr(T x, unsigned start, unsigned len) noexcept {
        if (sizeof(T) <= 4u) return static_cast<T>(mybit_bextr32(static_cast<uint32_t>(x), start, len));
        return static_cast<T>(mybit_bextr64(static_cast<uint64_t>(x), start, len));
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T pext(T x, T mask) noexcept {
        if (sizeof(T) <= 4u) return static_cast<T>(mybit_pext32(static_cast<uint32_t>(x), static_cast<uint32_t>(mask)));
        return static_cast<T>(mybit_pext64(static_cast<uint64_t>(x), static_cast<uint64_t>(mask)));
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T pdep(T x, T mask) noexcept {
        if (sizeof(T) <= 4u) return static_cast<T>(mybit_pdep32(static_cast<uint32_t>(x), static_cast<uint32_t>(mask)));
        return static_cast<T>(mybit_pdep64(static_cast<uint64_t>(x), static_cast<uint64_t>(mask)));
    }

    template<typename T, typename = _enable_unsigned<T>>
    constexpr T reverse(T x) noexcept {
#if defined(__cpp_lib_is_constant_evaluated) || (defined(__cplusplus) && __cplusplus >= 202002L)
        if (std::is_constant_evaluated()) {
            const unsigned bits = sizeof(T) * 8u;
            T r = 0;
            for (unsigned i = 0u; i < bits; ++i) {
                if ((x & (T(1) << i)) != 0) {
                    r |= (T(1) << (bits - 1u - i));
                }
            }
            return r;
        }
#endif
        if (sizeof(T) <= 4u) {
            return static_cast<T>(mybit_reverse32(static_cast<uint32_t>(x)) >> (32u - (sizeof(T) * 8u)));
        }
        return static_cast<T>(mybit_reverse64(static_cast<uint64_t>(x)));
    }
}
#endif /* __cplusplus */

#endif /* MYBIT_H */