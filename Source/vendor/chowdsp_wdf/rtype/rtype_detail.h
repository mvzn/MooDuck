#ifndef CHOWDSP_WDF_RTYPE_DETAIL_H
#define CHOWDSP_WDF_RTYPE_DETAIL_H

#include <array>
#include <tuple>
#include <vector>

namespace chowdsp
{
namespace rtype_detail
{
    /** Applies a function to each element in a tuple */
    template <typename Fn, typename Tuple, size_t... Ix>
    constexpr void forEachInTuple (Fn&& fn, Tuple&& tuple, std::index_sequence<Ix...>) noexcept (noexcept (std::initializer_list<int> { (fn (std::get<Ix> (tuple), Ix), 0)... }))
    {
        (void) std::initializer_list<int> { (fn (std::get<Ix> (tuple), Ix), 0)... };
    }

    template <typename Fn, typename Tuple>
    constexpr void forEachInTuple (Fn&& fn, Tuple&& tuple) noexcept (noexcept (forEachInTuple (std::forward<Fn> (fn), std::forward<Tuple> (tuple), std::make_index_sequence<std::tuple_size<std::remove_reference_t<Tuple>>::value> {})))
    {
        forEachInTuple (std::forward<Fn> (fn), std::forward<Tuple> (tuple), std::make_index_sequence<std::tuple_size<std::remove_reference_t<Tuple>>::value> {});
    }

    // Fixed-size aligned array for wdft (compile-time)
    template <typename T, int N, int alignment = 16>
    struct AlignedArray
    {
        void clear() noexcept { std::fill (arr.begin(), arr.end(), T {}); }
        T& operator[] (int i) noexcept { return arr[i]; }
        const T& operator[] (int i) const noexcept { return arr[i]; }

        std::array<T, N> arr {};
    };

    // Fixed-size matrix for wdft (compile-time)
    template <typename T, int N, int alignment = 16>
    struct Matrix
    {
        AlignedArray<T, N, alignment>& operator[] (int i) noexcept { return mat[i]; }
        const AlignedArray<T, N, alignment>& operator[] (int i) const noexcept { return mat[i]; }

        std::array<AlignedArray<T, N, alignment>, N> mat {};
    };

    // Scatter: b = S * a
    template <typename T, int N, int alignment>
    inline void RtypeScatter (Matrix<T, N, alignment>& S,
                              AlignedArray<T, N, alignment>& a,
                              AlignedArray<T, N, alignment>& b) noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            b[i] = T (0);
            for (int j = 0; j < N; ++j)
                b[i] += S[i][j] * a[j];
        }
    }

} // namespace rtype_detail
} // namespace chowdsp

#endif //CHOWDSP_WDF_RTYPE_DETAIL_H
