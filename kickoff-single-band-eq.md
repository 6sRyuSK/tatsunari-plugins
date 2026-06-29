# Kickoff — single-band parametric EQ (first vertical slice)

Paste this into Claude Code in the repo. **Read CLAUDE.md first.**

## Prerequisites (bootstrap if missing)
If the repo doesn't yet have the skeleton, create it first: a top-level
`CMakeLists.txt` that fetches JUCE 8 and adds a `core/` library and the plugin;
`tools/gen_catalog.py` (already written — drop it in); a `README.md` containing
the `<!-- BEGIN:CATALOG -->` / `<!-- END:CATALOG -->` markers; and an empty
`roadmap.toml`.

## Goal
Build the first plugin **end to end** as a vertical slice that exercises the whole
pipeline: manifest → spec + analytic oracle → DSP → pluginval → package → catalog.
Keep it minimal; the point is to prove the pipeline, not to ship a feature-rich EQ.

## The plugin
A **single-band parametric (peaking) EQ**, stereo in/out. Parameters:
- `frequency` — 20 Hz … 20 kHz, logarithmic, default 1 kHz
- `gain` — −24 … +24 dB, default 0 dB
- `q` — 0.1 … 18, default 0.707
- `bypass` — bool

One RBJ peaking biquad per channel. No additional bands. A minimal generic editor
is fine — no custom graphics.

## DSP spec — RBJ peaking EQ (Q parameterization)
Per channel, at the current sample rate `Fs`:
```
A     = 10^(gain_dB / 40)
w0    = 2*pi*frequency / Fs
alpha = sin(w0) / (2*Q)

b0 = 1 + alpha*A
b1 = -2*cos(w0)
b2 = 1 - alpha*A
a0 = 1 + alpha/A
a1 = -2*cos(w0)
a2 = 1 - alpha/A
```
Normalize `b0,b1,b2,a1,a2` by `a0` and run as a biquad (TDF-II is fine). Recompute
coefficients on parameter change **off** the audio thread and hand them to the
audio thread lock-free (RT-safety rules in CLAUDE.md).

## The oracle — INDEPENDENT (do not reuse the DSP class)
In the test code, write a **separate** reference that:
1. Computes the RBJ coefficients from the formulas above (independently written —
   not a call into the DSP implementation).
2. Evaluates the **discrete** transfer function at `z = e^{jω}`, `ω = 2π f / Fs`:
   ```
   H(e^jω) = (b0 + b1·e^{-jω} + b2·e^{-2jω}) / (a0 + a1·e^{-jω} + a2·e^{-2jω})
   ```
   yielding expected magnitude `20·log10|H|` and phase over a frequency grid.

## Tests (must have teeth)
1. **Oracle vs measurement.** Feed a unit impulse into the DSP core, capture the
   impulse response, FFT it, and compare to the oracle over the frequency grid.
   Magnitude is the primary gate (tolerance e.g. ±0.05 dB). Compare phase via the
   **complex** response to avoid wrap artifacts. If a relaxation is needed within a
   few bins of Nyquist, justify it in a comment — do not just widen it to pass.
2. **Formula-independent invariants** (exact, tight tolerance), over several
   `(frequency, gain, Q)` combinations:
   - magnitude at `frequency` == `gain` dB
   - magnitude at DC == 0 dB
   - magnitude at Nyquist == 0 dB
3. Run 1 and 2 at `Fs` = 44100, 48000, 96000 (recompute the oracle per `Fs`).

## Acceptance criteria (the pipeline)
1. `plugins/single-band-eq/plugin.toml` created: name, slug `single-band-eq`,
   category `EQ`, status `in-progress`, version `0.1.0`, formats `["VST3", "AU"]`.
2. CMake reads `version` from `plugin.toml` (regex per the catalog kit) and
   `juce_add_plugin` builds VST3 (and AU on macOS).
3. The DSP core class is separate from the `AudioProcessor`, unit-testable headless,
   and RT-safe.
4. The oracle + invariant tests pass at all three sample rates, registered with CTest.
5. `pluginval --strictness-level 5 --skip-gui-tests` passes for the built VST3
   (and the AU on macOS).
6. `python tools/gen_catalog.py` run; the plugin now shows under **In progress** in
   the README.
7. Open a PR with all of the above.

## Hard rules
- Do **not** weaken tolerances, add to `disabled-tests`, or alter the oracle to go
  green. If a test fails, fix the DSP — or stop and report what's failing.
- Do **not** expand scope.

## Out of scope
Multiple bands, shelving/HP/LP types, custom UI graphics, CLAP/AAX, presets.
A single peaking band only.