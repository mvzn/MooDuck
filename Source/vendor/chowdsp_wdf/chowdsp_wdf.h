#pragma once

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4464 4514 4324)
#endif

// define maybe_unused if we can
#if __cplusplus >= 201703L
#define CHOWDSP_WDF_MAYBE_UNUSED [[maybe_unused]]
#else
#define CHOWDSP_WDF_MAYBE_UNUSED
#endif

// Default SIMD alignment (no xsimd dependency)
constexpr int CHOWDSP_WDF_DEFAULT_SIMD_ALIGNMENT = 16;

#include "wdft/wdft.h"
#include "rtype/rtype.h"
#include "util/defer_impedance.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
