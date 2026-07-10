#ifndef WAVEDIGITALFILTERS_DEFER_IMPEDANCE_H
#define WAVEDIGITALFILTERS_DEFER_IMPEDANCE_H

#include "../wdft/wdft_base.h"
#include "../rtype/rtype_detail.h"

namespace chowdsp
{
namespace wdft
{
    /**
     * Defers impedance propagation for a set of WDF elements while in scope.
     * Useful when updating multiple parameter changes at once to avoid
     * redundant intermediate re-calculations.
     */
    template <typename... Elements>
    class ScopedDeferImpedancePropagation
    {
    public:
        explicit ScopedDeferImpedancePropagation (Elements&... elems) : elements (std::tie (elems...))
        {
#if __cplusplus >= 201703L
            static_assert ((std::is_base_of<BaseWDF, Elements>::value && ...), "All element types must be derived from BaseWDF");
#endif
            chowdsp::rtype_detail::forEachInTuple (
                [] (auto& el, size_t) {
                    el.dontPropagateImpedance = true;
                },
                elements);
        }

        ~ScopedDeferImpedancePropagation()
        {
            chowdsp::rtype_detail::forEachInTuple (
                [] (auto& el, size_t) {
                    el.dontPropagateImpedance = false;
                    el.calcImpedance();
                },
                elements);
        }

    private:
        std::tuple<Elements&...> elements;
    };

} // namespace wdft
} // namespace chowdsp

#endif //WAVEDIGITALFILTERS_DEFER_IMPEDANCE_H
