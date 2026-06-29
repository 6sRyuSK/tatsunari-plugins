#pragma once
//
// factory_core/Waveshaper.h — shared DSP primitive: a memoryless, odd-symmetric
// soft-clip saturator with drive / mix / output controls. Header-only,
// JUCE-independent, headless-testable. The transfer function is pure (no state),
// so it is safe to call per-sample on the audio thread and to oversample around.
//
#include <cmath>

namespace factory_core
{
    // Per-sample transfer:  output * ( (1-mix)*x + mix * tanh(drive * x) )
    //
    // - tanh is the soft-clip nonlinearity; drive pushes the signal into it.
    // - mix blends the dry signal with the shaped (wet) signal, linearly.
    // - output is a final trim.
    //
    // Because tanh is odd and the dry term is linear, the whole transfer is
    // odd-symmetric:  f(-x) == -f(x). A pure tone therefore produces only odd
    // harmonics — a property the tests assert.
    class Waveshaper
    {
    public:
        void setDrive  (double linearGain) noexcept { drive  = linearGain; }
        void setMix    (double zeroToOne)  noexcept { mix    = zeroToOne; }
        void setOutput (double linearGain) noexcept { output = linearGain; }

        inline double processSample (double x) const noexcept
        {
            const double wet = std::tanh (drive * x);
            return output * ((1.0 - mix) * x + mix * wet);
        }

        template <typename Sample>
        void process (Sample* samples, int numSamples) const noexcept
        {
            for (int i = 0; i < numSamples; ++i)
                samples[i] = static_cast<Sample> (processSample (static_cast<double> (samples[i])));
        }

    private:
        double drive  { 1.0 };
        double mix    { 1.0 };
        double output { 1.0 };
    };
} // namespace factory_core
