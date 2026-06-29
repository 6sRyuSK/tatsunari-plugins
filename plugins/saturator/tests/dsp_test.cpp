//
// dsp_test.cpp — headless verification of the saturator DSP core
// (factory_core::Waveshaper). The plugin is non-linear, so the gates are:
//
//   1. Transfer-curve invariants (formula-independent): f(0)=0, odd symmetry
//      f(-x) = -f(x), strict monotonicity, boundedness at mix=1, and the
//      small-signal slope output*(1-mix+mix*drive).
//   2. Mix wiring: endpoints (mix=0 -> output*x, mix=1 -> output*tanh(drive*x))
//      and linear interpolation between them.
//   3. Harmonic structure measured by running a pure tone through the core:
//      an odd-symmetric shaper produces only ODD harmonics (even harmonics ~0),
//      and THD grows monotonically with drive.
//
// The tone is placed at an integer number of cycles per block so every harmonic
// lands exactly on a DFT bin (no leakage), and low enough that the significant
// harmonics stay below Nyquist (so base-rate aliasing does not contaminate the
// even-harmonic check).
//
#include "factory_core/Waveshaper.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    using cd = std::complex<double>;
    constexpr double kPi = 3.14159265358979323846;

    int g_failures = 0;
    void fail (const std::string& m) { std::printf ("  FAIL: %s\n", m.c_str()); ++g_failures; }

    factory_core::Waveshaper makeShaper (double drive, double mix, double output)
    {
        factory_core::Waveshaper w;
        w.setDrive (drive);
        w.setMix (mix);
        w.setOutput (output);
        return w;
    }

    // Direct DFT bin via phase recurrence: sum x[n] e^{-j w n}.
    cd dftAt (const std::vector<double>& x, double w)
    {
        const cd step = std::exp (cd (0.0, -w));
        cd zk (1.0, 0.0);
        cd acc (0.0, 0.0);
        for (const double xn : x) { acc += xn * zk; zk *= step; }
        return acc;
    }

    void transferInvariants()
    {
        std::printf ("Transfer-curve invariants\n");
        const double drives[]  = { 0.5, 1.0, 2.0, 5.0, 10.0 };
        const double mixes[]   = { 0.0, 0.25, 0.5, 1.0 };
        const double outputs[] = { 0.5, 1.0, 2.0 };

        for (double drive : drives)
            for (double mix : mixes)
                for (double output : outputs)
                {
                    const auto w = makeShaper (drive, mix, output);
                    const std::string tag = "d=" + std::to_string (drive) + " m=" + std::to_string (mix)
                                          + " o=" + std::to_string (output);

                    // f(0) = 0
                    if (std::abs (w.processSample (0.0)) > 1e-12)
                        fail (tag + ": f(0) != 0");

                    // Odd symmetry and monotonicity over a grid.
                    double prev = w.processSample (-4.0);
                    for (int i = -40; i <= 40; ++i)
                    {
                        const double x = i * 0.1;
                        const double fp = w.processSample (x);
                        const double fn = w.processSample (-x);
                        if (std::abs (fp + fn) > 1e-12)
                            fail (tag + ": not odd-symmetric at x=" + std::to_string (x));
                        if (i > -40 && fp < prev - 1e-15)
                            fail (tag + ": not monotonic at x=" + std::to_string (x));
                        prev = fp;
                    }

                    // Small-signal slope: output*(1-mix+mix*drive).
                    const double eps = 1e-5;
                    const double slope = (w.processSample (eps) - w.processSample (-eps)) / (2.0 * eps);
                    const double expected = output * (1.0 - mix + mix * drive);
                    if (std::abs (slope - expected) > 1e-4 * std::max (1.0, std::abs (expected)))
                        fail (tag + ": slope@0 " + std::to_string (slope) + " != " + std::to_string (expected));

                    // Boundedness at mix=1: |f| <= output.
                    if (mix == 1.0)
                        for (double x : { -100.0, -10.0, 10.0, 100.0 })
                            if (std::abs (w.processSample (x)) > output * (1.0 + 1e-9))
                                fail (tag + ": exceeds output bound at x=" + std::to_string (x));

                    // Mix wiring: f = (1-mix)*output*x + mix*output*tanh(drive*x).
                    for (double x : { -2.0, -0.3, 0.7, 3.0 })
                    {
                        const double dry = output * x;
                        const double wet = output * std::tanh (drive * x);
                        const double ref = (1.0 - mix) * dry + mix * wet;
                        if (std::abs (w.processSample (x) - ref) > 1e-12)
                            fail (tag + ": mix wiring mismatch at x=" + std::to_string (x));
                    }
                }
        std::printf ("  done (%d combos)\n", (int) (5 * 4 * 3));
    }

    // THD from odd harmonics (3,5,...) relative to the fundamental.
    double measureHarmonics (double Fs, double drive, int N, int cycles, double amp, bool& evenClean)
    {
        const auto w = makeShaper (drive, 1.0, 1.0);
        const double w0 = 2.0 * kPi * cycles / N; // fundamental angular freq (integer bin)

        std::vector<double> y ((size_t) N);
        for (int n = 0; n < N; ++n)
            y[(size_t) n] = w.processSample (amp * std::sin (w0 * n));

        const double fund = std::abs (dftAt (y, w0));

        // Even harmonics must be ~0 (odd-symmetric shaper).
        evenClean = true;
        for (int k = 2; k * cycles * 2 < N; k += 2) // even k, below Nyquist
        {
            const double mag = std::abs (dftAt (y, w0 * k));
            if (mag > 1e-6 * fund) { evenClean = false; }
        }

        // THD: RMS of odd harmonics (k>=3) over the fundamental.
        double sumSq = 0.0;
        for (int k = 3; k * cycles * 2 < N; k += 2)
        {
            const double mag = std::abs (dftAt (y, w0 * k));
            sumSq += mag * mag;
        }
        return std::sqrt (sumSq) / fund;
    }

    void harmonicTests (double Fs)
    {
        std::printf ("Harmonic structure @ Fs=%.0f\n", Fs);
        const int N = 1 << 14;
        const int cycles = 64;          // f0 = 64*Fs/16384 ~ 187 Hz @48k: harmonics stay sub-Nyquist
        const double amp = 0.8;

        double prevThd = -1.0;
        for (double drive : { 2.0, 5.0, 10.0 })
        {
            bool evenClean = true;
            const double thd = measureHarmonics (Fs, drive, N, cycles, amp, evenClean);
            if (! evenClean)
                fail ("drive=" + std::to_string (drive) + ": even harmonics not ~0");
            if (prevThd >= 0.0 && thd <= prevThd)
                fail ("drive=" + std::to_string (drive) + ": THD not increasing with drive");
            std::printf ("  drive=%.1f  THD=%.4f  evenClean=%d\n", drive, thd, (int) evenClean);
            prevThd = thd;
        }
    }
}

int main (int argc, char** argv)
{
    transferInvariants();

    std::vector<double> rates;
    if (argc > 1) rates.push_back (std::atof (argv[1]));
    else          rates = { 44100.0, 48000.0, 96000.0 };
    for (double Fs : rates)
        harmonicTests (Fs);

    if (g_failures == 0) { std::printf ("OK: all checks passed.\n"); return 0; }
    std::printf ("FAILED: %d check(s).\n", g_failures);
    return 1;
}
