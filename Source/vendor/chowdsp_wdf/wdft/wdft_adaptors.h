#ifndef CHOWDSP_WDF_WDFT_ADAPTORS_H
#define CHOWDSP_WDF_WDFT_ADAPTORS_H

#include "wdft_base.h"

namespace chowdsp
{
namespace wdft
{
    /** WDF 3-port parallel adaptor */
    template <typename T, typename Port1Type, typename Port2Type>
    class WDFParallelT final : public BaseWDF
    {
    public:
        /** Creates a new WDF parallel adaptor from two connected ports. */
        WDFParallelT (Port1Type& p1, Port2Type& p2) : port1 (p1),
                                                       port2 (p2)
        {
            port1.connectToParent (this);
            port2.connectToParent (this);
            calcImpedance();
        }

        /** Computes the impedance for a WDF parallel adaptor.
         *  1     1     1
         * --- = --- + ---
         * Z_p   Z_1   Z_2
         */
        inline void calcImpedance() override
        {
            wdf.G = port1.wdf.G + port2.wdf.G;
            wdf.R = (T) 1.0 / wdf.G;
            port1Reflect = port1.wdf.G / wdf.G;
        }

        /** Accepts an incident wave into a WDF parallel adaptor. */
        inline void incident (T x) noexcept
        {
            const auto b2 = wdf.b - port2.wdf.b + x;
            port1.incident (b2 + bDiff);
            port2.incident (b2);

            wdf.a = x;
        }

        /** Propogates a reflected wave from a WDF parallel adaptor. */
        inline T reflected() noexcept
        {
            port1.reflected();
            port2.reflected();

            bDiff = port2.wdf.b - port1.wdf.b;
            wdf.b = port2.wdf.b - port1Reflect * bDiff;

            return wdf.b;
        }

        Port1Type& port1;
        Port2Type& port2;

        WDFMembers<T> wdf;

    private:
        T port1Reflect = (T) 1.0;
        T bDiff = (T) 0.0;
    };

    /** WDF 3-port series adaptor */
    template <typename T, typename Port1Type, typename Port2Type>
    class WDFSeriesT final : public BaseWDF
    {
    public:
        /** Creates a new WDF series adaptor from two connected ports. */
        WDFSeriesT (Port1Type& p1, Port2Type& p2) : port1 (p1),
                                                     port2 (p2)
        {
            port1.connectToParent (this);
            port2.connectToParent (this);
            calcImpedance();
        }

        /** Computes the impedance for a WDF series adaptor.
         * Z_s = Z_1 + Z_2
         */
        inline void calcImpedance() override
        {
            wdf.R = port1.wdf.R + port2.wdf.R;
            wdf.G = (T) 1.0 / wdf.R;
            port1Reflect = port1.wdf.R / wdf.R;
        }

        /** Accepts an incident wave into a WDF series adaptor. */
        inline void incident (T x) noexcept
        {
            const auto b1 = port1.wdf.b - port1Reflect * (x + port1.wdf.b + port2.wdf.b);
            port1.incident (b1);
            port2.incident (-(x + b1));

            wdf.a = x;
        }

        /** Propogates a reflected wave from a WDF series adaptor. */
        inline T reflected() noexcept
        {
            wdf.b = -(port1.reflected() + port2.reflected());
            return wdf.b;
        }

        Port1Type& port1;
        Port2Type& port2;

        WDFMembers<T> wdf;

    private:
        T port1Reflect = (T) 1.0;
    };

    /** WDF Voltage Polarity Inverter */
    template <typename T, typename PortType>
    class PolarityInverterT final : public BaseWDF
    {
    public:
        /** Creates a new WDF polarity inverter */
        explicit PolarityInverterT (PortType& p) : port1 (p)
        {
            port1.connectToParent (this);
            calcImpedance();
        }

        inline void calcImpedance() override
        {
            wdf.R = port1.wdf.R;
            wdf.G = (T) 1.0 / wdf.R;
        }

        inline void incident (T x) noexcept
        {
            wdf.a = x;
            port1.incident (-x);
        }

        inline T reflected() noexcept
        {
            wdf.b = -port1.reflected();
            return wdf.b;
        }

        WDFMembers<T> wdf;

    private:
        PortType& port1;
    };

    // Useful factory functions

    template <typename T, typename P1Type, typename P2Type>
    CHOWDSP_WDF_MAYBE_UNUSED WDFParallelT<T, P1Type, P2Type> makeParallel (P1Type& p1, P2Type& p2)
    {
        return WDFParallelT<T, P1Type, P2Type> (p1, p2);
    }

    template <typename T, typename P1Type, typename P2Type>
    CHOWDSP_WDF_MAYBE_UNUSED WDFSeriesT<T, P1Type, P2Type> makeSeries (P1Type& p1, P2Type& p2)
    {
        return WDFSeriesT<T, P1Type, P2Type> (p1, p2);
    }

    template <typename T, typename PType>
    CHOWDSP_WDF_MAYBE_UNUSED PolarityInverterT<T, PType> makeInverter (PType& p1)
    {
        return PolarityInverterT<T, PType> (p1);
    }

} // namespace wdft
} // namespace chowdsp

#endif //CHOWDSP_WDF_WDFT_ADAPTORS_H
