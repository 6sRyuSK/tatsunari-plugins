//
// dsp_test.cpp — headless verification of the resonance suppressor DSP core
// (factory_core::FFT + ResonanceSuppressor). This is a non-linear / adaptive
// processor, so the gates are invariants rather than a single closed-form oracle:
//
//   1. FFT round-trips to identity and matches a direct DFT.
//   2. STFT perfect reconstruction: depth=0 => output == input delayed by the
//      reported latency N (proves windowing / overlap-add / latency).
//   3. Delta: wet (mix=1) + removed (delta) == dry.
//   4. Suppression + selectivity: a strong steady tone over noise is reduced,
//      while a resonance-free control band is left alone.
//   5. Reduction profile: zeroing the profile around a resonance disables
//      suppression there (the "EQ-like" curve scales suppression locally).
//   6. Stereo link: identical L==R input yields identical L==R output.
//   7. Resolution vs sample rate: the order chosen by
//      factory_core::fftOrderForSampleRate keeps the analyser bin width and the
//      analysis-window length within bounds at every supported rate (incl.
//      192 kHz), so the low end of the spectrum is never lost. This is the guard
//      against a fixed FFT order silently degrading at high sample rates.
//   8. Detection mode. Soft (adaptive threshold) is level-independent: the same
//      resonance is cut by the same dB at 0 dB and −12 dB. Hard (absolute
//      threshold set by Depth) is level-dependent: a resonance is cut hard when
//      loud and left alone when quiet. Hard stays selective (resonance cut, not
//      broadband) and numerically safe (finite / non-increasing) at worst-case
//      Depth. Delta and the silence floor are re-checked in both modes.
//
// Every test runs across the full standard sample-rate matrix
// (44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz) and prepares the engine at the same
// order the plugin would pick (factory_core::fftOrderForSampleRate), so the
// gates exercise the real high-rate path.
//
#include "factory_core/FFT.h"
#include "factory_core/ResonanceSuppressor.h"
#include "factory_core/StftResolution.h"
#include "factory_core/testing/DspInvariants.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace
{
    using cd = std::complex<double>;
    constexpr double kPi = 3.14159265358979323846;

    int g_failures = 0;
    void fail (const std::string& m) { std::printf ("  FAIL: %s\n", m.c_str()); ++g_failures; }

    // The STFT order the plugin would use at this sample rate (mirrors
    // ResonanceSuppressorAudioProcessor). Tests prepare the engine with this so
    // they exercise the real high-rate path, not a fixed order.
    int orderFor (double Fs) { return factory_core::fftOrderForSampleRate (Fs, 11, 48000.0, 13); }

    // Magnitude of a real signal at frequency f (single-bin DFT over [a,b)).
    double magAt (const std::vector<double>& x, int a, int b, double f, double Fs)
    {
        cd acc {};
        const double w = 2.0 * kPi * f / Fs;
        for (int n = a; n < b; ++n) acc += x[(size_t) n] * std::exp (cd (0.0, -w * (n - a)));
        return std::abs (acc) / (b - a);
    }

    void fftTest()
    {
        std::printf ("FFT\n");
        std::mt19937 rng (1);
        std::uniform_real_distribution<double> u (-1.0, 1.0);

        for (int order : { 6, 8, 10 })
        {
            factory_core::FFT f; f.prepare (order);
            const int n = f.size();
            std::vector<cd> a ((size_t) n), b;
            for (auto& v : a) v = cd (u (rng), u (rng));
            b = a;
            f.forward (b.data());
            f.inverse (b.data());
            double e = 0.0;
            for (int i = 0; i < n; ++i) e = std::max (e, std::abs (b[(size_t) i] - a[(size_t) i]));
            if (e > 1.0e-9) fail ("FFT round-trip order " + std::to_string (order) + " err " + std::to_string (e));
        }

        // Forward vs direct DFT.
        factory_core::FFT f; f.prepare (6);
        const int n = f.size();
        std::vector<cd> a ((size_t) n), A;
        for (auto& v : a) v = cd (u (rng), u (rng));
        A = a; f.forward (A.data());
        double e = 0.0;
        for (int k = 0; k < n; ++k)
        {
            cd s {};
            for (int m = 0; m < n; ++m) s += a[(size_t) m] * std::exp (cd (0.0, -2.0 * kPi * k * m / n));
            e = std::max (e, std::abs (A[(size_t) k] - s));
        }
        if (e > 1.0e-9) fail ("FFT vs DFT err " + std::to_string (e));
        std::printf ("  ok\n");
    }

    // Render a stereo signal through a configured suppressor; return mono output.
    template <typename Cfg>
    std::vector<double> render (double Fs, const std::vector<double>& xl, const std::vector<double>& xr, Cfg cfg)
    {
        factory_core::ResonanceSuppressor s;
        s.prepare (Fs, orderFor (Fs));
        cfg (s);
        std::vector<double> out (xl.size());
        for (size_t n = 0; n < xl.size(); ++n)
        {
            double l = xl[n], r = xr[n];
            s.process (l, r);
            out[n] = 0.5 * (l + r);
        }
        return out;
    }

    void reconstructionTest (double Fs)
    {
        std::printf ("STFT reconstruction + latency @ Fs=%.0f\n", Fs);
        const int N = 1 << orderFor (Fs); // window = latency at the rate's order
        const int M = std::max (1 << 14, 4 * N);
        std::mt19937 rng (7);
        std::uniform_real_distribution<double> u (-0.5, 0.5);
        std::vector<double> x ((size_t) M);
        for (auto& v : x) v = u (rng);

        const auto y = render (Fs, x, x, [] (factory_core::ResonanceSuppressor& s) {
            s.setDepth (0.0); s.setMix (1.0);
        });

        factory_core::ResonanceSuppressor probe; probe.prepare (Fs, orderFor (Fs));
        if (probe.latencySamples() != N) fail ("latency != N");

        double e = 0.0;
        for (int n = 2 * N; n < M; ++n) e = std::max (e, std::abs (y[(size_t) n] - x[(size_t) (n - N)]));
        if (e > 1.0e-6) fail ("reconstruction (depth=0) err " + std::to_string (e));
        std::printf ("  maxErr=%.2e\n", e);
    }

    void deltaTest (double Fs, int mode)
    {
        std::printf ("Delta (wet + removed == dry) mode=%d @ Fs=%.0f\n", mode, Fs);
        const int N = 1 << orderFor (Fs);
        const int M = std::max (1 << 14, 4 * N);
        std::mt19937 rng (11);
        std::normal_distribution<double> g (0.0, 0.3);
        std::vector<double> x ((size_t) M);
        for (int n = 0; n < M; ++n) x[(size_t) n] = g (rng) + 0.5 * std::sin (2.0 * kPi * 1500.0 * n / Fs);

        const auto wet = render (Fs, x, x, [mode] (factory_core::ResonanceSuppressor& s) {
            s.setMode (mode); s.setDepth (1.0); s.setMix (1.0);
        });
        const auto rem = render (Fs, x, x, [mode] (factory_core::ResonanceSuppressor& s) {
            s.setMode (mode); s.setDepth (1.0); s.setDelta (true);
        });
        double e = 0.0;
        for (int n = 2 * N; n < M; ++n) e = std::max (e, std::abs (wet[(size_t) n] + rem[(size_t) n] - x[(size_t) (n - N)]));
        if (e > 1.0e-6) fail ("wet+removed != dry err " + std::to_string (e));
        std::printf ("  maxErr=%.2e\n", e);
    }

    void suppressionTest (double Fs)
    {
        std::printf ("Suppression + selectivity @ Fs=%.0f\n", Fs);
        const int M = 1 << 15;
        const double f0 = 2000.0;
        std::mt19937 rng (5);
        std::normal_distribution<double> g (0.0, 0.1);
        std::vector<double> x ((size_t) M);
        for (int n = 0; n < M; ++n) x[(size_t) n] = g (rng) + 0.5 * std::sin (2.0 * kPi * f0 * n / Fs);

        const auto dry = render (Fs, x, x, [] (factory_core::ResonanceSuppressor& s) { s.setDepth (0.0); });
        const auto wet = render (Fs, x, x, [] (factory_core::ResonanceSuppressor& s) {
            s.setDepth (1.2); s.setSharpness (0.5);
        });

        const int a = M / 2, b = M; // steady-state window
        const double dryF0 = magAt (dry, a, b, f0, Fs), wetF0 = magAt (wet, a, b, f0, Fs);

        // Control selectivity: measure a *broadband* resonance-free region (4–10
        // kHz) rather than a single bin. A single-frequency probe is
        // noise-realisation sensitive — a random spectral peak there gets
        // legitimately reduced — which makes the gate flaky across sample rates
        // (bin alignment differs per rate). Averaging the energy over the band
        // reflects the suppressor's actual broadband behaviour and is stable.
        double dryCtrlE = 0.0, wetCtrlE = 0.0;
        for (double fc : { 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0 })
        {
            const double d = magAt (dry, a, b, fc, Fs), w = magAt (wet, a, b, fc, Fs);
            dryCtrlE += d * d; wetCtrlE += w * w;
        }
        const double f0Db   = 20.0 * std::log10 (wetF0 / dryF0);
        const double ctrlDb = 10.0 * std::log10 (wetCtrlE / (dryCtrlE + 1e-30));

        if (f0Db > -4.4)
            fail ("resonance at f0 not suppressed (>=4.4 dB): " + std::to_string (f0Db) + " dB");
        // The control band must be left broadly intact, and the resonance must be
        // cut far harder than the control band (the point of a resonance suppressor).
        if (ctrlDb < -4.5)
            fail ("control band over-attenuated (not selective): " + std::to_string (ctrlDb) + " dB");
        if (f0Db > ctrlDb - 15.0)
            fail ("not selective: f0 " + std::to_string (f0Db) + " dB vs control " + std::to_string (ctrlDb) + " dB");
        std::printf ("  f0 %.1f dB   control %.1f dB (broadband)\n", f0Db, ctrlDb);
    }

    void profileTest (double Fs)
    {
        std::printf ("Reduction profile (local scaling) @ Fs=%.0f\n", Fs);
        const int M = 1 << 15;
        const double f0 = 2000.0;
        std::mt19937 rng (9);
        std::normal_distribution<double> g (0.0, 0.1);
        std::vector<double> x ((size_t) M);
        for (int n = 0; n < M; ++n) x[(size_t) n] = g (rng) + 0.5 * std::sin (2.0 * kPi * f0 * n / Fs);

        const auto dry = render (Fs, x, x, [] (factory_core::ResonanceSuppressor& s) { s.setDepth (0.0); });
        // Profile zero around f0 -> no suppression there despite the resonance.
        const auto masked = render (Fs, x, x, [Fs, f0] (factory_core::ResonanceSuppressor& s) {
            s.setDepth (1.2);
            std::vector<double> prof ((size_t) s.numBins(), 1.0);
            const int N = 1 << factory_core::fftOrderForSampleRate (Fs, 11, 48000.0, 13);
            const int kf = (int) std::round (f0 * (double) N / Fs);
            for (int k = kf - 12; k <= kf + 12; ++k)
                if (k >= 0 && k < s.numBins()) prof[(size_t) k] = 0.0;
            s.setProfile (prof.data(), s.numBins());
        });

        const int a = M / 2, b = M;
        const double dryF0 = magAt (dry, a, b, f0, Fs), mF0 = magAt (masked, a, b, f0, Fs);
        if (mF0 < dryF0 * 0.85) fail ("profile=0 region still suppressed "
                                      + std::to_string (20.0 * std::log10 (mF0 / dryF0)) + " dB");
        std::printf ("  masked f0 %.2f dB (expect ~0)\n", 20.0 * std::log10 (mF0 / dryF0));
    }

    void stereoLinkTest (double Fs)
    {
        std::printf ("Stereo link (L==R) @ Fs=%.0f\n", Fs);
        const int M = 1 << 14;
        std::mt19937 rng (3);
        std::normal_distribution<double> g (0.0, 0.2);
        std::vector<double> x ((size_t) M);
        for (int n = 0; n < M; ++n) x[(size_t) n] = g (rng) + 0.5 * std::sin (2.0 * kPi * 3000.0 * n / Fs);

        factory_core::ResonanceSuppressor s; s.prepare (Fs, orderFor (Fs));
        s.setDepth (1.2); s.setStereoLink (true);
        double e = 0.0;
        for (int n = 0; n < M; ++n)
        {
            double l = x[(size_t) n], r = x[(size_t) n];
            s.process (l, r);
            e = std::max (e, std::abs (l - r));
        }
        if (e > 1.0e-9) fail ("linked L/R diverged err " + std::to_string (e));
        std::printf ("  maxLRdiff=%.2e\n", e);
    }

    // Regression guard for issue #24: a near-silent source (e.g. a synth's idle
    // output at ~-100 dBFS) that still has spectral peaks must NOT trigger any
    // gain reduction. A purely relative peak-vs-envelope detector would paint a
    // phantom reduction "curtain" on it; the absolute floor must keep the engine
    // idle so silence stays silent on the display and in the audio.
    void silenceTest (double Fs, int mode)
    {
        std::printf ("Silence floor (no reduction on near-silent input) mode=%d @ Fs=%.0f\n", mode, Fs);
        const int M = 1 << 15;
        const double amp = 1.0e-5; // ~ -100 dBFS peaks — inaudible
        std::mt19937 rng (13);
        std::normal_distribution<double> g (0.0, amp * 0.1);
        std::vector<double> x ((size_t) M);
        for (int n = 0; n < M; ++n)
            x[(size_t) n] = g (rng)
                          + amp * std::sin (2.0 * kPi * 400.0 * n / Fs)
                          + amp * std::sin (2.0 * kPi * 800.0 * n / Fs);

        factory_core::ResonanceSuppressor s; s.prepare (Fs, orderFor (Fs));
        s.setMode (mode); s.setDepth (1.5); s.setSharpness (0.5);
        for (int n = 0; n < M; ++n) { double l = x[(size_t) n], r = l; s.process (l, r); }

        const double* red = s.reductionDb();
        double worst = 0.0; // most negative reduction across all bins
        for (int k = 0; k < s.numBins(); ++k) worst = std::min (worst, red[(size_t) k]);
        if (worst < -0.5)
            fail ("phantom reduction on near-silent input: " + std::to_string (worst)
                  + " dB (mode " + std::to_string (mode) + ") at Fs=" + std::to_string (Fs));
        std::printf ("  worstReduction=%.3f dB (expect ~0)\n", worst);
    }

    // Regression guard for the 192 kHz analyser bug: a fixed FFT order makes the
    // bin width (fs/N) and window length (N/fs) drift with the sample rate. The
    // order chosen by fftOrderForSampleRate must keep both within bounds at every
    // rate, so the analyser always has a data point near 20 Hz and the
    // suppressor's detection window stays ~constant in time.
    void resolutionTest (double Fs)
    {
        std::printf ("Analyser resolution invariants @ Fs=%.0f\n", Fs);
        const int order = orderFor (Fs);

        factory_core::ResonanceSuppressor s; s.prepare (Fs, order);
        const double binHz = s.binToHz (1);                 // lowest non-DC analyser bin
        const double winMs = 1000.0 * s.latencySamples() / Fs;

        // Bin 1 must reach the bottom of the audible band so a 20 Hz feature is
        // representable; otherwise the analyser's low end goes blank (the bug).
        if (binHz > 25.0)
            fail ("bin width too coarse: " + std::to_string (binHz) + " Hz at Fs=" + std::to_string (Fs));
        // Window length (and thus reduction behaviour) must stay ~constant in time.
        if (winMs < 30.0 || winMs > 60.0)
            fail ("window length out of range: " + std::to_string (winMs) + " ms at Fs=" + std::to_string (Fs));

        std::printf ("  order=%d binHz=%.2f winMs=%.1f\n", order, binHz, winMs);
    }

    // Bin-aligned tone frequency near `f0` so windowed magnitude has no scalloping
    // loss — the Hard-mode tests key off absolute dBFS, so the tone must land on a
    // bin for the level threshold to be exercised precisely.
    double alignedFreq (double Fs, double f0)
    {
        const int N = 1 << orderFor (Fs);
        const int kf = std::max (1, (int) std::round (f0 * (double) N / Fs));
        return (double) kf * Fs / (double) N;
    }

    // Measure the steady-state reduction (dB) of a resonance at `f0` for a given
    // mode/depth and input scale, via an independent single-bin DFT (magAt).
    double resonanceReductionDb (double Fs, double f0, int mode, double depth, double scale)
    {
        const int M = 1 << 15;
        std::mt19937 rng (5);
        std::normal_distribution<double> g (0.0, 0.1);
        std::vector<double> x ((size_t) M);
        for (int n = 0; n < M; ++n)
            x[(size_t) n] = scale * (g (rng) + 0.5 * std::sin (2.0 * kPi * f0 * n / Fs));

        const auto dry = render (Fs, x, x, [] (factory_core::ResonanceSuppressor& s) { s.setDepth (0.0); });
        const auto wet = render (Fs, x, x, [mode, depth] (factory_core::ResonanceSuppressor& s) {
            s.setMode (mode); s.setDepth (depth); s.setSharpness (0.5);
        });
        const int a = M / 2, b = M;
        return 20.0 * std::log10 (magAt (wet, a, b, f0, Fs) / magAt (dry, a, b, f0, Fs));
    }

    // Soft mode uses an adaptive threshold, so its reduction is invariant to input
    // level: the SAME resonance is cut by the SAME dB whether the signal is at
    // 0 dB or −12 dB. This is the defining property of the frequency-dependent
    // (Soft) mode — assert it directly, with no oracle (issue #34 style: the
    // property itself is the gate, measured through the real DSP).
    void softLevelInvarianceTest (double Fs)
    {
        std::printf ("Soft mode level invariance @ Fs=%.0f\n", Fs);
        const double f0 = alignedFreq (Fs, 2000.0);
        const double loud = resonanceReductionDb (Fs, f0, 0, 1.2, 1.0);   // hot
        const double quiet = resonanceReductionDb (Fs, f0, 0, 1.2, 0.25);  // −12 dB
        if (loud > -4.0)
            fail ("Soft: resonance not suppressed at 0 dB: " + std::to_string (loud) + " dB");
        if (std::abs (loud - quiet) > 0.5)
            fail ("Soft mode not level-invariant: loud " + std::to_string (loud)
                  + " dB vs quiet " + std::to_string (quiet) + " dB at Fs=" + std::to_string (Fs));
        std::printf ("  redF0 loud=%.2f dB  quiet=%.2f dB (expect ~equal)\n", loud, quiet);
    }

    // Hard mode reacts to absolute level: Depth sets an absolute threshold, so the
    // same resonance is cut hard when it sits above the threshold (loud) and left
    // essentially alone when it drops below it (quiet). This is the defining
    // property of the level-dependent (Hard) mode. Depth ~0.28 puts the threshold
    // near −16 dBFS, between the loud (−6 dBFS) and quiet (−26 dBFS) resonance.
    void hardLevelDependenceTest (double Fs)
    {
        std::printf ("Hard mode level dependence @ Fs=%.0f\n", Fs);
        const double f0 = alignedFreq (Fs, 2000.0);
        const double loud  = resonanceReductionDb (Fs, f0, 1, 0.28, 1.0);  // −6 dBFS peak
        const double quiet = resonanceReductionDb (Fs, f0, 1, 0.28, 0.1);  // −26 dBFS peak
        if (loud > -4.0)
            fail ("Hard: loud resonance not suppressed: " + std::to_string (loud) + " dB");
        if (quiet < -1.5)
            fail ("Hard: quiet resonance should be nearly untouched: " + std::to_string (quiet) + " dB");
        if (quiet - loud < 4.0)
            fail ("Hard mode not level-dependent: loud " + std::to_string (loud)
                  + " dB vs quiet " + std::to_string (quiet) + " dB at Fs=" + std::to_string (Fs));
        std::printf ("  redF0 loud=%.2f dB  quiet=%.2f dB (expect loud << quiet)\n", loud, quiet);
    }

    // Hard mode must still be a *resonance* suppressor, not a broadband gate: a hot
    // resonance is cut hard while a resonance-free control band is left broadly
    // intact (selectivity), measured with an independent DFT.
    void hardSuppressionTest (double Fs)
    {
        std::printf ("Hard suppression + selectivity @ Fs=%.0f\n", Fs);
        const int M = 1 << 15;
        const double f0 = alignedFreq (Fs, 2000.0);
        std::mt19937 rng (5);
        std::normal_distribution<double> g (0.0, 0.1);
        std::vector<double> x ((size_t) M);
        for (int n = 0; n < M; ++n) x[(size_t) n] = g (rng) + 0.5 * std::sin (2.0 * kPi * f0 * n / Fs);

        const auto dry = render (Fs, x, x, [] (factory_core::ResonanceSuppressor& s) { s.setDepth (0.0); });
        const auto wet = render (Fs, x, x, [] (factory_core::ResonanceSuppressor& s) {
            s.setMode (1); s.setDepth (0.8); s.setSharpness (0.5);
        });

        const int a = M / 2, b = M;
        const double f0Db = 20.0 * std::log10 (magAt (wet, a, b, f0, Fs) / magAt (dry, a, b, f0, Fs));
        double dryCtrlE = 0.0, wetCtrlE = 0.0;
        for (double fc : { 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0 })
        {
            const double d = magAt (dry, a, b, fc, Fs), w = magAt (wet, a, b, fc, Fs);
            dryCtrlE += d * d; wetCtrlE += w * w;
        }
        const double ctrlDb = 10.0 * std::log10 (wetCtrlE / (dryCtrlE + 1e-30));
        if (f0Db > -6.0)
            fail ("Hard: resonance at f0 not suppressed (>=6 dB): " + std::to_string (f0Db) + " dB");
        if (ctrlDb < -4.5)
            fail ("Hard: control band over-attenuated (not selective): " + std::to_string (ctrlDb) + " dB");
        if (f0Db > ctrlDb - 15.0)
            fail ("Hard: not selective: f0 " + std::to_string (f0Db) + " dB vs control " + std::to_string (ctrlDb) + " dB");
        std::printf ("  f0 %.1f dB   control %.1f dB (broadband)\n", f0Db, ctrlDb);
    }

    // Hard mode is feed-forward (no feedback loop), but assert it anyway at the
    // worst-case Depth across all rates: the impulse-response tail must not grow
    // (loop gain < 1) and a long hot hold must stay finite and bounded (no NaN /
    // runaway), per the regression-policy numeric-safety invariants.
    void hardStabilityTest (double Fs)
    {
        std::printf ("Hard mode stability (finite / non-increasing) @ Fs=%.0f\n", Fs);
        factory_core::ResonanceSuppressor s; s.prepare (Fs, orderFor (Fs));
        s.setMode (1); s.setDepth (1.5); s.setSharpness (0.5); s.setMix (1.0);
        auto proc = [&s] (double in) { double l = in, r = in; s.process (l, r); return l; };
        if (! factory_core::testing::impulseResponseNonIncreasing (proc, Fs, 4.0, 0.25, 1.05))
            fail ("Hard mode impulse response grew (loop gain >= 1) at Fs=" + std::to_string (Fs));

        // Long hot noise hold: output must stay finite and bounded (a suppressor
        // only attenuates, so a realistic ceiling — not a 1e6 not-NaN tolerance).
        factory_core::ResonanceSuppressor s2; s2.prepare (Fs, orderFor (Fs));
        s2.setMode (1); s2.setDepth (1.5); s2.setSharpness (0.5); s2.setMix (1.0);
        const int M = (int) (2.0 * Fs);
        std::mt19937 rng (23);
        std::normal_distribution<double> g (0.0, 0.5);
        std::vector<double> y ((size_t) M);
        for (int n = 0; n < M; ++n) { double l = g (rng), r = l; s2.process (l, r); y[(size_t) n] = l; }
        if (! factory_core::testing::allFinite (y))
            fail ("Hard mode produced non-finite output at Fs=" + std::to_string (Fs));
        if (factory_core::testing::peakAbs (y) > 4.0)
            fail ("Hard mode output exceeded realistic bound: "
                  + std::to_string (factory_core::testing::peakAbs (y)) + " at Fs=" + std::to_string (Fs));

        // Resolution-follows-rate holds for the mode path too (independent of mode).
        if (! factory_core::testing::resolutionFollowsSampleRate (Fs, 25.0, 0.030))
            fail ("resolution out of range at Fs=" + std::to_string (Fs));
        std::printf ("  ok (peak=%.3f)\n", factory_core::testing::peakAbs (y));
    }
}

int main (int argc, char** argv)
{
    // Full standard sample-rate matrix, up to 192 kHz. A single rate may be
    // passed on the command line (CTest registers one case per rate).
    std::vector<double> rates;
    if (argc > 1) rates.push_back (std::atof (argv[1]));
    else          rates = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    fftTest();
    for (double Fs : rates)
    {
        reconstructionTest (Fs);
        deltaTest (Fs, 0);   // Soft
        deltaTest (Fs, 1);   // Hard
        suppressionTest (Fs);
        profileTest (Fs);
        stereoLinkTest (Fs);
        silenceTest (Fs, 0); // Soft
        silenceTest (Fs, 1); // Hard
        resolutionTest (Fs);
        softLevelInvarianceTest (Fs);
        hardLevelDependenceTest (Fs);
        hardSuppressionTest (Fs);
        hardStabilityTest (Fs);
    }

    if (g_failures == 0) { std::printf ("OK: all checks passed.\n"); return 0; }
    std::printf ("FAILED: %d check(s).\n", g_failures);
    return 1;
}
