#ifndef MMX_H_
#define MMX_H_

#include <x86.h>
//#include <intrin.h>
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
# include <immintrin.h>
#else
/* No x86 SIMD on this host (e.g. the PS Vita's ARMv7-A + NEON). sse2neon.h is a
 * single-header shim that implements the SSE/SSE2 intrinsic surface on top of
 * ARM NEON, and it provides __m128i + every _mm_* the generated code uses
 * (_mm_cvtsi32_si128, _mm_slli_epi64, _mm_madd_epi16, _mm_packs_epi32, ...).
 * The vita/ CMake fetches it onto the include path. */
# define SSE2NEON_SUPPRESS_WARNINGS 1
# include <sse2neon.h>
#endif

namespace x86
{

struct regmmx
{
    __m128i reg;
    operator x86::reg64() const
    {
        return _mm_cvtsi128_si64(reg);
    }
    operator __m128i() const
    {
        return reg;
    }
};

static inline regmmx from_reg64(const x86::reg64& value)
{
    regmmx result;
    result.reg = _mm_cvtsi64_si128(value);
    return result;
}

struct MMX
{
    regmmx mm0;
    regmmx mm1;
    regmmx mm2;
    regmmx mm3;
    regmmx mm4;
    regmmx mm5;
    regmmx mm6;
    regmmx mm7;
    void init()
    {
        memset(this, 0, sizeof(*this));
    }
};

}

#endif /* !MMX_H_ */
