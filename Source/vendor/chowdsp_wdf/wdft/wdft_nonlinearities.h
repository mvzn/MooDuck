#ifndef CHOWDSP_WDF_WDFT_NONLINEARITIES_H
#define CHOWDSP_WDF_WDFT_NONLINEARITIES_H

#include "wdft_base.h"
#include <cmath>

namespace chowdsp
{
namespace wdft
{
    /** WDF Switch (non-linear) */
    template <typename T, typename Next>
    class SwitchT final : public RootWDF
    {
    public:
        explicit SwitchT (Next& next)
        {
            next.connectToParent (this);
            calcImpedance();
        }

        void calcImpedance() override {}

        void setClosed (bool shouldBeClosed) { closed = shouldBeClosed; }

        inline void incident (T x) noexcept { wdf.a = x; }

        inline T reflected() noexcept
        {
            wdf.b = closed ? -wdf.a : wdf.a;
            return wdf.b;
        }

        WDFMembers<T> wdf;

    private:
        bool closed = true;
    };

} // namespace wdft
} // namespace chowdsp

#endif //CHOWDSP_WDF_WDFT_NONLINEARITIES_H
