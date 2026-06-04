// Standalone harmonic probe for the P2 two-tube makeup amp (KorenTriode12AX7).
// Drives a 1 kHz sine through the real processBlock (4x oversampled) and reports
// H1..H5 (normalised to H1) + THD, to compare against the SPICE oracle's
// even-dominant fingerprint (H2 >> H3). Not part of the CI suite.
//
// Build (after `make` has produced tests/build/*.o):
//   g++ -std=c++17 -O2 -Wno-deprecated-declarations -pthread -fvisibility=hidden \
//       -DLINUX=1 -DNDEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
//       -DJUCE_STANDALONE_APPLICATION=0 \
//       -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_events=1 \
//       -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 \
//       -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 \
//       -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
//       -DJUCE_MODULE_AVAILABLE_juce_dsp=1 \
//       -I. -I/home/mokram/JUCE/modules -I../Source -I../Source/vendor \
//       measure_sat.cpp build/juce_core.o build/juce_events.o \
//       build/juce_data_structures.o build/juce_audio_basics.o \
//       build/juce_audio_formats.o build/juce_dsp.o build/juce_core_CompilationTime.o \
//       -lpthread -ldl -lrt -lcurl -o measure_sat && ./measure_sat

#include "JuceHeader.h"
#include "KorenTriode.h"
#include <cstdio>
#include <cmath>
#include <vector>

static void goertzelMags(const std::vector<float>& x, double fs, double f0,
                         int maxH, double mags[6])
{
    const int N = (int) x.size();
    for (int h = 1; h <= maxH; ++h)
    {
        double re = 0.0, im = 0.0;
        const double w = 2.0 * M_PI * (h * f0) / fs;
        for (int n = 0; n < N; ++n)
        {
            re += x[n] * std::cos(w * n);
            im += x[n] * std::sin(w * n);
        }
        mags[h] = (2.0 / N) * std::sqrt(re * re + im * im);
    }
}

static void measure(float drive, float inAmp)
{
    const double fs   = 48000.0;
    const double f0   = 1000.0;
    const int    blk  = 512;
    const int    warm = 48000;          // 1 s settle (DC blocker etc.)
    const int    N    = 4800;           // 100 whole cycles @ 1 kHz / 48 k

    KorenTriode12AX7 amp;
    amp.prepare(fs, blk);
    amp.setDrive(drive);
    amp.setEnabled(true);

    std::vector<float> out;
    out.reserve(N);

    long n = 0;
    std::vector<float> L(blk), R(blk);
    auto fill = [&](int count)
    {
        for (int i = 0; i < count; ++i, ++n)
            L[i] = R[i] = (float) (inAmp * std::sin(2.0 * M_PI * f0 * (double) n / fs));
    };

    int produced = 0;
    while (produced < warm + N)
    {
        fill(blk);
        amp.processBlock(L.data(), R.data(), blk);
        for (int i = 0; i < blk && produced < warm + N; ++i, ++produced)
            if (produced >= warm)
                out.push_back(L[i]);
    }

    double m[6] = {0,0,0,0,0,0};
    goertzelMags(out, fs, f0, 5, m);

    const double h1 = (m[1] > 1e-12) ? m[1] : 1e-12;
    double thd = 0.0;
    for (int h = 2; h <= 5; ++h) thd += m[h] * m[h];
    thd = std::sqrt(thd) / h1 * 100.0;

    auto dB = [](double r){ return 20.0 * std::log10(std::max(r, 1e-9)); };
    std::printf("drive=%-4.2f inAmp=%-4.2f | H1=%.4f  THD=%6.2f%%  "
                "H2=%6.2fdB  H3=%6.2fdB  H4=%6.2fdB  H5=%6.2fdB  | H2-H3=%5.1fdB\n",
                drive, inAmp, h1, thd,
                dB(m[2]/h1), dB(m[3]/h1), dB(m[4]/h1), dB(m[5]/h1),
                dB(m[2]/h1) - dB(m[3]/h1));
}

// P3 breathing probe: feed a sudden sustained loud tone and report the output
// peak in successive windows — expect a droop over tau≈235ms (dynamic cathode
// bias) settling to a compressed steady state, then recovery after the tone stops.
static void measureBreathing()
{
    const double fs = 48000.0;
    const int    blk = 64;
    const double f0 = 1000.0;
    KorenTriode12AX7 amp;
    amp.prepare(fs, blk);
    amp.setDrive(1.0f);
    amp.setEnabled(true);

    const double inAmp = 0.9;
    const int total = (int)(fs * 1.2);    // 1.2 s of tone
    std::vector<float> env;               // per-sample |out|, smoothed lightly

    std::vector<float> L(blk), R(blk);
    long n = 0;
    int produced = 0;
    double pk = 0.0;
    std::vector<std::pair<double,double>> marks = {
        {0.000,0.010},{0.050,0.060},{0.120,0.130},{0.235,0.245},
        {0.470,0.480},{0.800,0.810},{1.100,1.110}};
    std::vector<double> peaks(marks.size(), 0.0);

    while (produced < total)
    {
        for (int i = 0; i < blk; ++i, ++n)
            L[i] = R[i] = (float)(inAmp * std::sin(2.0*M_PI*f0*(double)n/fs));
        amp.processBlock(L.data(), R.data(), blk);
        for (int i = 0; i < blk; ++i, ++produced)
        {
            const double t = (double)produced / fs;
            const double v = std::fabs(L[i]);
            for (size_t k = 0; k < marks.size(); ++k)
                if (t >= marks[k].first && t < marks[k].second)
                    peaks[k] = std::max(peaks[k], v);
        }
    }
    (void)pk;
    std::printf("--- P3 dynamic cathode bias (breathing): output peak vs time, loud 0.9 tone ---\n");
    const double ref = peaks[0] > 1e-9 ? peaks[0] : 1e-9;
    for (size_t k = 0; k < marks.size(); ++k)
        std::printf("  t=%.0f-%.0f ms : peak=%.4f  (%+5.2f dB vs onset)\n",
                    marks[k].first*1000, marks[k].second*1000, peaks[k],
                    20.0*std::log10(std::max(peaks[k],1e-9)/ref));
}

int main()
{
    std::printf("=== P2 makeup-amp harmonic probe (oracle target: even-dominant, H2 >> H3) ===\n");
    std::printf("--- sweep DRIVE at fixed input amplitude 0.7 ---\n");
    for (float d : { 0.5f, 1.0f, 2.0f, 4.0f })
        measure(d, 0.7f);
    std::printf("--- sweep INPUT amplitude at drive 1.0 ---\n");
    for (float a : { 0.1f, 0.3f, 0.7f, 1.0f })
        measure(1.0f, a);
    std::printf("\n");
    measureBreathing();
    return 0;
}
