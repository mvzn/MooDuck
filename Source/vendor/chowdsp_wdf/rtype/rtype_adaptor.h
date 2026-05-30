#ifndef CHOWDSP_WDF_RTYPE_ADAPTOR_H
#define CHOWDSP_WDF_RTYPE_ADAPTOR_H

#include "../wdft/wdft_base.h"
#include "rtype_detail.h"

namespace chowdsp
{
namespace wdft
{
    /**
     *  An adaptable R-Type adaptor.
     *  For more information see: https://searchworks.stanford.edu/view/11891203, chapter 2
     *
     *  Not used in MaybeDuck's EQP-1A tree (all junctions decompose to binary
     *  series/parallel pairs), but included for completeness.
     */
    template <typename T, int upPortIndex, typename ImpedanceCalculator, typename... PortTypes>
    class RtypeAdaptor : public BaseWDF
    {
    public:
        static constexpr auto numPorts = int (sizeof...(PortTypes) + 1);

        explicit RtypeAdaptor (PortTypes&... dps) : downPorts (std::tie (dps...))
        {
            b_vec.clear();
            a_vec.clear();

            chowdsp::rtype_detail::forEachInTuple (
                [&] (auto& port, size_t) { port.connectToParent (this); },
                downPorts);
        }

        void calcImpedance() override
        {
            wdf.R = ImpedanceCalculator::calcImpedance (*this);
            wdf.G = (T) 1 / wdf.R;
        }

        constexpr auto getPortImpedances()
        {
            std::array<T, numPorts - 1> portImpedances {};
            chowdsp::rtype_detail::forEachInTuple (
                [&] (auto& port, size_t i) { portImpedances[i] = port.wdf.R; },
                downPorts);
            return portImpedances;
        }

        void setSMatrixData (const T (&mat)[numPorts][numPorts])
        {
            for (int i = 0; i < numPorts; ++i)
                for (int j = 0; j < numPorts; ++j)
                    S_matrix[i][j] = mat[i][j];
        }

        inline void incident (T downWave) noexcept
        {
            wdf.a = downWave;
            a_vec[upPortIndex] = wdf.a;

            chowdsp::rtype_detail::RtypeScatter (S_matrix, a_vec, b_vec);
            chowdsp::rtype_detail::forEachInTuple (
                [&] (auto& port, size_t i) {
                    auto portIndex = getPortIndex ((int) i);
                    port.incident (b_vec[portIndex]);
                },
                downPorts);
        }

        inline T reflected() noexcept
        {
            chowdsp::rtype_detail::forEachInTuple (
                [&] (auto& port, size_t i) {
                    auto portIndex = getPortIndex ((int) i);
                    a_vec[portIndex] = port.reflected();
                },
                downPorts);

            wdf.b = b_vec[upPortIndex];
            return wdf.b;
        }

        WDFMembers<T> wdf;

    private:
        constexpr auto getPortIndex (int tupleIndex)
        {
            return tupleIndex < upPortIndex ? tupleIndex : tupleIndex + 1;
        }

        std::tuple<PortTypes&...> downPorts;

        chowdsp::rtype_detail::Matrix<T, numPorts> S_matrix;
        chowdsp::rtype_detail::AlignedArray<T, numPorts> a_vec;
        chowdsp::rtype_detail::AlignedArray<T, numPorts> b_vec;
    };

} // namespace wdft
} // namespace chowdsp

#endif //CHOWDSP_WDF_RTYPE_ADAPTOR_H
