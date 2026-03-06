# Roland Juno-6 VCF (Voltage Controlled Filter) — VST Implementation Reference

## Overview

The Juno-6 VCF is a 4-pole (24dB/octave) low-pass filter built around the Roland IR3109 chip — a custom quad OTA (operational transconductance amplifier) with on-chip exponential converter and high-impedance FET buffers. Resonance is handled externally by a BA662 OTA in a negative feedback loop. The filter is capable of self-oscillation and features loudness compensation to counteract the passband volume drop at high resonance.

The Juno-6 also has a separate, simple high-pass filter (HPF) in the signal path — a continuously variable passive RC filter controlled by a slider.

---

## IR3109 Chip Architecture

The IR3109 is a 16-pin DIP IC containing four independent OTA + buffer stages controlled from a single exponential current source. It was manufactured by International Rectifier to Roland's specifications and used across a wide range of Roland instruments including the Jupiter-8, Jupiter-6, Juno-6/60, Juno-106 (inside the 80017A module), SH-101, and MC-202.

Key characteristics:

| Parameter | Value |
|---|---|
| Topology | 4× OTA + FET buffer, cascaded for 4-pole LPF |
| Control law | On-chip exponential converter (V-to-I) |
| Controllable range | >15 octaves |
| Supply | ±15V (bipolar), or single-supply with virtual ground on some models |
| Package | 16-pin DIP |
| Resonance | Not on-chip — requires external VCA (BA662 on Juno-6) |

The four OTA stages are cascaded in series, with each stage contributing one pole (6dB/octave). The output of stage 4 is the 24dB/octave low-pass output. On the Jupiter-8, a 12dB/octave output was also available by tapping after stage 2 (using the stronger buffers on stages 2 and 4). The Juno-6 only uses the 4-pole output.

---

## Juno-6 Filter Component Values

| Component | Value | Notes |
|---|---|---|
| Input resistors (per stage) | 68kΩ | Standard across most IR3109 Roland implementations |
| Internal feedback resistors | 560Ω | Built into the IR3109 on the + input |
| Stage capacitors | 240pF | Ceramic disc; E24 value (220pF is close substitute at ~9% shift) |
| Resonance VCA | BA662 OTA | External to IR3109; includes unused buffer section |
| Frequency CV | Exponential, via on-chip converter | Single CV input controls all four stages simultaneously |
| Temperature compensation | Tempco resistor in CV divider | Improves pitch tracking stability |

The capacitor type matters for character. Roland used cheap ceramic disc capacitors in nearly all IR3109 implementations (polystyrene only in the SH-7). Ceramics have slight nonlinear voltage coefficients which contribute to the filter's character under high signal levels.

---

## Cutoff Frequency Range and Control

The IR3109's on-chip exponential converter provides a single control current that sets the transconductance of all four OTAs simultaneously. The cutoff frequency is determined by:

```
f_cutoff = gm / (2π × C)
```

Where `gm` is the OTA transconductance (proportional to the control current) and `C` is the stage capacitor (240pF).

The exponential converter gives a 1V/octave-like response: the frequency doubles for each fixed increment of control voltage. This provides a usable range spanning approximately 15+ octaves, from sub-audio (~2 Hz) to well above audible (~30 kHz+), though in practice the Juno-6 panel slider covers roughly 10 Hz to 20+ kHz.

### Cutoff CV Mixing

The Juno-6 cutoff frequency is determined by the sum of several CV sources, mixed by an inverting op-amp summing circuit before reaching the IR3109's exponential converter input:

- **FREQ slider**: Manual cutoff position (0–5V)
- **ENV amount** (slider, bipolar): Scaled ADSR envelope output
- **LFO amount** (via VCF switch): LFO modulation depth
- **Keyboard follow** (KYBD switch): Pitch CV tracking, selectable amount
- **External CV** (rear panel jack): Direct voltage control input

The CV mixing happens in voltage domain before the exponential converter, so all sources add in the pitch (log-frequency) domain — a 1V addition from the envelope and a 1V addition from keyboard follow each shift the cutoff by the same number of octaves.

### Keyboard Follow

The Juno-6 has a keyboard follow switch with typically three positions controlling how much the filter tracks pitch. At full tracking, the cutoff follows the keyboard at approximately 1V/octave, keeping the timbral character consistent across the keyboard. This is essential for self-oscillation tuning.

---

## Resonance Circuit

The Juno-6's resonance is implemented as a negative feedback loop from the 4-pole output back to the filter input, with the feedback level controlled by a BA662 OTA.

### Signal Flow

```
Input → [Summing node] → IR3109 (4-pole LPF) → Output
              ↑                                      |
              |                                      |
              +──── BA662 (resonance VCA) ←──────────+
              |           ↑
              |      Resonance CV
              |
              +──── BA662 (compensation) ← Input (scaled)
```

Two key features of the Juno-6 resonance implementation:

**Negative feedback (inverted)**: The 4-pole filter provides 180° of phase shift at the cutoff frequency. The BA662 inverts the feedback signal, giving a total of 360° (= 0°) phase shift. This creates positive feedback at the cutoff frequency, producing the resonant peak.

**Loudness drop compensation (Q compensation)**: The Juno-6 circuit feeds a portion of the *input* signal through the resonance VCA alongside the feedback signal. As resonance increases, this boosts the input drive level, compensating for the passband volume loss that occurs with high resonance. This is the "classic" Roland approach and contributes significantly to the Juno's character — it means driving the filter harder at high resonance, which pushes the OTA nonlinearities more.

### Self-Oscillation

At maximum resonance, the feedback gain exceeds unity at the cutoff frequency and the filter self-oscillates, producing a clean sine wave. The amplitude of self-oscillation is limited by the OTA's inherent tanh() nonlinearity (the differential transistor pair inside each OTA saturates at high signal levels). This soft clipping is what gives the self-oscillation its particular character — it's not a hard clip, but a gentle fold that adds subtle even harmonics.

When self-oscillating, the filter can be "played" as a sine oscillator using keyboard follow. Hardware clones report approximately 8 octaves of reasonably accurate pitch tracking.

---

## Nonlinear Behavior (OTA tanh Saturation)

This is the most important characteristic for faithful emulation. Each OTA stage in the IR3109 contains a differential transistor pair whose transfer function is:

```
I_out = I_ctrl × tanh(V_diff / (2 × Vt))
```

Where `V_diff` is the differential input voltage, `I_ctrl` is the control current (set by the exponential converter), and `Vt` is the thermal voltage (~26mV at room temperature).

In practice, for DSP modeling, this is typically normalized:

```
I_out = g × tanh(V_in - V_out)
```

Where `g = tan(π × f_cutoff / f_sample)` for trapezoidal integration.

### What the Nonlinearity Does

- **At low signal levels**: The tanh() is approximately linear and the filter behaves like an ideal 4-pole LPF.
- **At high signal levels**: The tanh() saturates, reducing the effective gain. This acts as a slew rate limiter on each stage.
- **In the feedback path**: The resonance amplitude is self-limiting due to saturation, giving the characteristic soft self-oscillation behavior.
- **Input drive effects**: Pushing more signal into the filter (e.g., via the Q compensation circuit at high resonance) causes the cutoff to shift slightly upward under the nonlinearity, and adds subtle harmonic content.
- **Cutoff modulation**: The nonlinearity effectively acts as signal-dependent cutoff modulation — large signals momentarily shift the filter's operating point.

The specific character of the Juno-6 filter (often described as "smooth" and "warm") comes from this soft tanh saturation distributed across all four stages.

---

## DSP Implementation

### One-Pole OTA-C Stage (Nonlinear)

Each filter stage is a one-pole lowpass using trapezoidal integration with tanh() nonlinearity:

```
struct OTAStage:
    s: float = 0.0     // state (integrator memory)

    function process(input, g):
        // g = tan(π × cutoff / sampleRate)
        // Nonlinear OTA model
        x = tanh(input - s)
        y = s + g * x
        s = s + 2.0 * g * x      // trapezoidal update
        // Note: the above is a simplified form.
        // More accurate: solve y = (s + g*tanh(input - y)) / (1 + g)
        // via Newton-Raphson iteration
        return y
```

For better accuracy, use the implicit form and solve with 1–2 Newton-Raphson iterations:

```
function process_NR(input, g):
    // Initial estimate (linear)
    y_est = (s + g * (input - s)) / (1 + g)

    // Newton-Raphson refinement (1-2 iterations)
    for i in 1..2:
        t = tanh(input - y_est)
        dt = 1.0 - t * t                  // sech²
        f = y_est - s - g * t
        df = 1.0 + g * dt
        y_est = y_est - f / df

    y = y_est
    s = 2.0 * y - s                        // state update
    return y
```

### Four-Pole Cascade with Resonance

```
struct Juno6VCF:
    stage: [OTAStage; 4]       // four cascaded stages
    resonance: float            // 0.0 to ~4.0 (self-oscillation around 4.0)
    input_gain: float           // from resonance compensation

    function process(input, cutoff, resonance, sampleRate):
        g = tan(PI * cutoff / sampleRate)

        // Q compensation: boost input at high resonance
        comp = 1.0 + resonance * 0.5    // approximate compensation curve
        x = input * comp

        // Feedback from stage 4 output (inverted)
        x = x - resonance * stage[3].last_output

        // Cascade through 4 stages
        y = x
        for i in 0..3:
            y = stage[i].process_NR(y, g)

        return y
```

### Oversampling

The tanh() nonlinearity generates harmonics that will alias. For clean results, 2× oversampling is the minimum; 4× is recommended for high-resonance settings. Use a quality anti-aliasing filter on the up/downsample boundaries.

### Parameter Scaling

For the VST's cutoff parameter, use an exponential mapping to match the 1V/octave nature of the IR3109's control law:

```
cutoff_hz = min_freq × 2^(slider_normalized × num_octaves)
```

Where `min_freq` ≈ 10 Hz and `num_octaves` ≈ 11 gives a range of ~10 Hz to ~20 kHz.

---

## High-Pass Filter (HPF)

The Juno-6 has a continuously variable HPF controlled by a slider (unlike the Juno-60/106 which have a 4-position switch). The Juno-6 HPF is a simple passive first-order (6dB/octave) RC high-pass filter placed in the signal path after the mixer and before the VCF input.

The cutoff range is approximately:

| Slider Position | Approximate Cutoff |
|---|---|
| 0 (off) | No filtering (~full bass) |
| ~3 | ~225 Hz |
| ~5 | ~340 Hz |
| ~8 | ~720 Hz |
| 10 (max) | ~1 kHz+ |

These values are approximate and extrapolated from the Juno-60/106 fixed positions which share the same underlying component values.

### DSP Implementation

A simple one-pole highpass:

```
struct HPF:
    s: float = 0.0

    function process(input, cutoff, sampleRate):
        g = tan(PI * cutoff / sampleRate)
        v = (input - s) / (1.0 + g)
        hp = v
        s = s + 2.0 * g * v      // state update
        return hp
```

The HPF is subtle but important for the Juno sound — even at low settings it tightens the low end and prevents the filter from getting muddy, especially with the sub-oscillator engaged.

---

## Signal Path Summary

The complete Juno-6 voice filter chain:

```
Oscillator mix → HPF (6dB/oct) → VCF (24dB/oct LPF + resonance) → VCA → Chorus → Output
                                      ↑
                              Cutoff CV sum:
                              FREQ + ENV + LFO + KYBD + EXT
```

---

## Characteristic Sonic Behaviors to Emulate

1. **Smooth resonance sweep**: The resonance sweep should be even and progressive, not abrupt. The Q compensation keeps the volume consistent.

2. **Warm self-oscillation**: At max resonance, the filter produces a clean-ish sine with subtle soft distortion from the OTA saturation. It should not be a perfect mathematical sine, but very close.

3. **Filter drive at high resonance**: The Q compensation circuit pushes more signal into the filter at high resonance, causing subtle saturation. This is a key part of the Juno's warmth.

4. **Cutoff shift with signal level**: Due to the distributed tanh() nonlinearities, the effective cutoff frequency shifts slightly with input level. Louder signals cause the cutoff to creep upward, which is audibly subtle but contributes to the "alive" quality of the filter.

5. **Keyboard follow tuning**: Self-oscillation should track pitch accurately across ~8 octaves when keyboard follow is enabled at 100%.

6. **HPF interaction**: The HPF is before the VCF, so high HPF settings reduce the bass content entering the resonant filter, which changes the resonance character (less bass to feed back).

---

## Sources

1. **Electric Druid — "Roland filter designs with the IR3109 or AS3109"**
   https://electricdruid.net/roland-filter-designs-with-the-ir3109-or-as3109/
   Detailed analysis of the Juno-6 filter circuit including component values (68kΩ/560Ω/240pF), resonance implementation via BA662, Q compensation topology, comparison across Jupiter-8/Juno-6/SH-101 implementations, and notes on the 2-pole vs 4-pole output tapping on the Jupiter-8.

2. **AMSynths — "All about the IR3109 chip"**
   https://amsynths.co.uk/2022/04/06/all-about-the-ir3109-chip/
   History and overview of the IR3109 across all Roland implementations, filter capacitor type discussion (ceramic disc vs polystyrene), the D80017A hybrid module in the Juno-106, and the transition to the IR3R05. Notes on capacitor matching and its effect on resonance/self-oscillation quality.

3. **Florian Anwander — "Roland Filters"**
   https://www.florian-anwander.de/roland_filters/
   Comprehensive table of every Roland synthesizer's filter type, technology, and circuit topology. Confirms Juno-6 uses four OTA stages integrated in the IR3109 with no changes across the production run.

4. **Obsolete Technology — "80017A VCF/VCA Teardown"**
   https://obsoletetechnology.wordpress.com/projects/80017a-vcfvca-teardown/
   Physical teardown of the Juno-106 hybrid IC confirming it contains one IR3109 and two BA662 chips — identical topology to the Juno-6/60 discrete implementation. Confirms component value compatibility between the Juno-60 and Juno-106 filter sections.

5. **A to Synth — "Juno filter analysis"**
   http://atosynth.blogspot.com/2017/10/juno-filter-analysis.html
   LTSpice simulation of the IR3109 OTA-C filter stage, measurements of the linear CV-to-frequency relationship (~75µA per 2.5kHz), and comparison of 240pF vs 330pF capacitor values across different Roland models. Notes on the 68kΩ/560Ω resistor values being consistent across most IR3109 implementations.

6. **AS3109 / IR3109 Datasheets (via Alfa Rpar / Erica Synths / Infinite Machinery)**
   https://www.ericasynths.lv/shop/ics/voltage-controlled-filter-3109/
   https://www.infinitemachinery.com/as3109
   AS3109 clone datasheet: >15 octave controllable range, low control voltage feedthrough (-45dB typical), four independent OTA+buffer sections with single exponential control input. Confirms high-impedance buffer architecture.

7. **Mod Wiggler / KVR Audio — "How to test Roland IR3109" / OTA filter modeling threads**
   https://modwiggler.com/forum/viewtopic.php?t=250834
   https://www.kvraudio.com/forum/viewtopic.php?t=370838
   https://www.kvraudio.com/forum/viewtopic.php?amp=&f=33&t=349859
   Community discussion on IR3109 testing (68kΩ/560Ω input configuration, 0V-range CV), OTA tanh() nonlinearity modeling in DSP, slew-rate limiting behavior, and cheap non-iterative zero-delay-feedback filter implementations by Teemu Voipio (mystran) and Urs Heckmann.

8. **Urs Heckmann — "One Pole Unlimited"**
   https://urs.silvrback.com/one-pole-unlimited
   Detailed derivation of the OTA nonlinear one-pole lowpass model, the distinction between OTA tanh(V+ - V-) vs transistor ladder tanh(V+) - tanh(V-), trapezoidal integration for zero-delay-feedback implementation, and the role of the 68kΩ resistors as a virtual buffer isolating the inverting input feedforward. Foundation reference for VA filter DSP.

9. **SoundForce — "VCF/VCA 6" (Eurorack Juno-60/106 adaptation)**
   https://sound-force.nl/?page_id=3763
   HPF cutoff frequencies (225Hz / 339Hz / 720Hz at fixed positions), self-oscillation onset at ~90% resonance, 8-octave V/Oct tracking range, and notes on the identical VCF topology between the Juno-60 and Juno-106.

---

## Caveats

- The IR3109 has no published datasheet from Roland. All circuit analysis is based on the Alfa AS3109 clone datasheet, schematic analysis, and community reverse-engineering.
- The exact shape of the tanh() nonlinearity in the original IR3109 may differ slightly from a textbook tanh due to transistor matching, bias conditions, and temperature. The standard tanh model is the accepted best approximation.
- The Q compensation gain curve (how much the input is boosted vs. resonance level) is circuit-specific and depends on the BA662 control law. The `1.0 + resonance * 0.5` factor in the pseudocode is a starting approximation — tuning against hardware recordings is recommended.
- The Juno-6 HPF is a continuously variable slider, while the Juno-60 has a 4-position switch (off/1/2/3). Some references conflate the two — the cutoff values listed are from the Juno-60/106 fixed positions and should be used as calibration points for the Juno-6 slider range.
- The ceramic disc capacitors used by Roland have a voltage coefficient (capacitance decreases slightly at higher voltages) which adds a subtle, signal-dependent frequency modulation. This is extremely difficult to model and is generally omitted in VST implementations without audible consequence.
- The self-oscillation sine wave quality depends heavily on the matching of the four 240pF capacitors. Slight mismatches cause the poles to spread slightly, which alters the onset and shape of self-oscillation. This per-voice variation is part of the analog character of real hardware but is typically not modeled in VSTs.
