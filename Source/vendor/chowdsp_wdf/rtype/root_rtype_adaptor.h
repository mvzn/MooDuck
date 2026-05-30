#ifndef CHOWDSP_WDF_ROOT_RTYPE_ADAPTOR_H
#define CHOWDSP_WDF_ROOT_RTYPE_ADAPTOR_H

#include "../wdft/wdft_base.h"
#include "rtype_detail.h"

namespace chowdsp
{
namespace wdft
{
    /**
     *  A non-adaptable R-Type adaptor.
     *  For more information see: https://searchworks.stanford.edu/view/11891203, chapter 2
     *
     *  Not used in MaybeDuck's EQP-1A tree (all junctions decompose to binary
     *  series/parallel pairs), but included for completeness.
     */
    template <typename T, typename ImpedanceCalculator, typename... PortTypes>
    class RootRtypeAdaptor : public RootWDF
    {
    public:
        static constexpr auto numPorts = int (sizeof...(PortTypes));

        explicit RootRtypeAdaptor (PortTypes&... dps) : downPorts (std::tie (dps...))
        {
            b_vec.clear();
            a_vec.clear();

            chowdsp::rtype_detail::forEachInTuple (
                [&] (auto& port, size_t) { port.connectToParent (this); },
                downPorts);
        }

        void calcImpedance() override
        {
            ImpedanceCalculator::calcImpedance (*this);
        }

        constexpr auto getPortImpedances()
        {
            std::array<T, numPorts> portImpedances {};
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

        inline void compute() noexcept
        {
            chowdsp::rtype_detail::RtypeScatter (S_matrix, a_vec, b_vec);
            chowdsp::rtype_detail::forEachInTuple (
                [&] (auto& port, size_t i) {
                    port.incident (b_vec[i]);
                    a_vec[i] = port.reflected();
                },
                downPorts);
        }

    private:
        std::tuple<PortTypes&...> downPorts;

        chowdsp::rtype_detail::Matrix<T, numPorts> S_matrix;
        chowdsp::rtype_detail::AlignedArray<T, numPorts> a_vec;
        chowdsp::rtype_detail::AlignedArray<T, numPorts> b_vec;
    };

} // namespace wdft
} // namespace chowdsp

#endif //CHOWDSP_WDF_ROOT_RTYPE_ADAPTOR_H
