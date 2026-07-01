#pragma once
//
// factory_core/StftResolution.h — choose an STFT / analysis FFT order that keeps
// the frequency resolution (bin width = fs / N) and the analysis-window length
// (N / fs seconds) roughly constant across sample rates.
//
// A *fixed* FFT order looks fine at 44.1 / 48 kHz but silently degrades at high
// sample rates: an order-11 (N = 2048) window has 23 Hz bins at 48 kHz but
// 93.75 Hz bins at 192 kHz, so a spectrum analyser then has no data point below
// ~94 Hz and is 4x coarser across the whole band, while the suppressor's
// detection window shrinks from ~43 ms to ~11 ms. Scaling the order with the
// sample rate (one extra order per octave of sample rate) holds ~23 Hz bins and
// a ~43 ms window at every supported rate.
//
// Pure, header-only, JUCE-independent — unit-testable without a plugin host.
//
#include <algorithm>
#include <cmath>

namespace factory_core
{
    // Pick the FFT order whose bin width matches the reference (refSampleRate /
    // 2^baseOrder) as closely as possible at `sampleRate`, clamped to
    // [baseOrder, maxOrder]. Doubling the sample rate adds one to the order (N
    // doubles), so fs / N — and therefore the analyser resolution and window
    // length in seconds — stays put.
    //
    // Defaults: order 11 (N = 2048) at the 48 kHz reference, capped at order 13
    // (N = 8192) which keeps ~23 Hz bins through 192 kHz.
    inline int fftOrderForSampleRate (double sampleRate,
                                      int    baseOrder     = 11,
                                      double refSampleRate = 48000.0,
                                      int    maxOrder      = 13) noexcept
    {
        if (sampleRate <= 0.0 || refSampleRate <= 0.0)
            return baseOrder;

        const int steps = (int) std::lround (std::log2 (sampleRate / refSampleRate));
        return std::clamp (baseOrder + steps, baseOrder, maxOrder);
    }
} // namespace factory_core
