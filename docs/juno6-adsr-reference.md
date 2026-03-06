# Roland Juno-6 ADSR Envelope Generator — VST Implementation Reference

## Overview

The Roland Juno-6 (1982) uses a custom IR3R01 envelope generator IC per voice. This document provides the circuit-level parameters, timing data, curve shapes, and pot taper math needed to faithfully emulate the Juno-6 ADSR in a software synthesizer plugin.

The IR3R01 is a 16-pin custom Roland chip that shares architectural DNA with the CEM3310 (Curtis) and the earlier Jupiter-4 CMOS oscillator-based ADSR, but is not directly equivalent to either. It generates a positive-going 0 to ~7.5V envelope output with exponential attack, decay, and release segments.

---

## Circuit Parameters (Juno-6 Specific)

| Parameter | Value | Notes |
|---|---|---|
| Supply voltage | ±15V | Standard Roland bipolar supply |
| Timing capacitor | 47nF | Sets base timing (68nF on Jupiter-8, 68nF on MKS-7) |
| Reference voltage | +7.5V | Tied to pin 7 (+10V on Jupiter-8, +5V on MKS-7) |
| Attack CV range | 0–5V | From DAC via multiplexer |
| Decay CV range | 0–5V | From DAC via multiplexer |
| Release CV range | 0–5V | From DAC via multiplexer |
| Sustain CV range | 0–10V | Level, not time |
| D/R CV input resistors | 22kΩ | (19kΩ on Jupiter-8; this difference contributes to the longer D/R range) |
| Max attack time | ~3 seconds | Trimmer-adjusted per voice for matching |
| Max decay time | ~12 seconds | |
| Max release time | ~12 seconds | |
| Min attack time | ~1 ms | |
| Min decay/release time | ~2 ms | |
| Output peak voltage | ~7.5V | Determined by reference voltage |
| Modulation pin | Grounded | Only used on Jupiter-8 for key follow |
| Voices | 6 | Each with its own IR3R01 |

---

## Front Panel Pot Taper

The Juno-6 uses **50kΩ linear** slide potentiometers for Attack, Decay, and Release with a **4.7kΩ resistor wired between pin 1 (ground-side) and pin 2 (wiper)**. This creates a reverse-log (anti-log) taper that concentrates fine control at the slow/long end of the slider travel. Roland used the same trick on the Jupiter-4 and Jupiter-8.

Sustain uses a standard linear pot (it controls level, not time).

### Taper Transfer Function

For a linear pot of resistance `R_pot` with shunt resistor `R_shunt` between the ground terminal and the wiper:

```
R_upper = (1 - x) * R_pot        // resistance above wiper
R_lower = x * R_pot               // resistance below wiper (pot only)
R_lower_par = (R_lower * R_shunt) / (R_lower + R_shunt)  // parallel combination

V_out / V_in = R_lower_par / (R_upper + R_lower_par)
```

Where `x` is the normalized slider position (0.0 = bottom/CCW, 1.0 = top/CW).

With `R_pot = 50000` and `R_shunt = 4700`:

```
function potTaper(x):
    if x <= 0: return 0
    if x >= 1: return 1
    R_upper = (1 - x) * 50000
    R_lower = x * 50000
    R_lower_par = (R_lower * 4700) / (R_lower + 4700)
    return R_lower_par / (R_upper + R_lower_par)
```

### Taper Lookup Table

| Slider Position | CV Output (volts) | Fraction of Max |
|---|---|---|
| 0 (min) | 0.00V | 0.000 |
| 1 | 0.42V | 0.084 |
| 2 | 0.78V | 0.155 |
| 3 | 1.09V | 0.218 |
| 4 | 1.37V | 0.275 |
| 5 | 1.64V | 0.328 |
| 6 | 1.89V | 0.378 |
| 7 | 2.14V | 0.429 |
| 8 | 2.40V | 0.480 |
| 9 | 2.66V | 0.533 |
| 10 (max) | 5.00V | 1.000 |

Note: the curve is concave — the CV rises steeply at first, then levels off. This is the *inverse* of the IR3R01's internal exponential oscillator law, so the two curves largely cancel out.

---

## Slider-to-Time Mapping

### Why the Pot Taper and Oscillator Law Cancel

The IR3R01 uses an internal oscillator whose frequency is exponentially related to the CV input (inherited from the Jupiter-4 CMOS oscillator architecture). Higher CV = higher oscillator frequency = shorter envelope time. This exponential law means that without correction, the slider would have almost all its useful range crammed into the last few percent of travel.

Roland's solution was the 4.7kΩ shunt resistor, which adds a *reverse-log* taper to the pot — a curve that is approximately the inverse of the oscillator's exponential response. The AMSynths article states this was added "rather than getting the microprocessor to do this job."

**The net result is that the two curves (reverse-log taper × exponential oscillator) approximately cancel**, producing a clean logarithmic (perceptually even) distribution of time across the slider throw. Each equal increment of slider travel roughly doubles the envelope time.

### Recommended DSP Model

For VST implementation, the pot taper and oscillator law should NOT be modeled as separate stages that compound. Instead, model the net slider-to-time relationship directly as a simple exponential-in-slider-position:

```
function sliderToTime(sliderPosition, minTime, maxTime):
    // sliderPosition: 0.0 (fastest) to 1.0 (slowest)
    return minTime * (maxTime / minTime) ^ sliderPosition
```

Convention: slider at 0 = fastest (minimum time), slider at 10 = slowest (maximum time).

### Time Lookup Table

| Slider | Attack Time | Decay/Release Time |
|---|---|---|
| 0 | 1.0 ms | 2.0 ms |
| 1 | 2.2 ms | 4.8 ms |
| 2 | 5.0 ms | 11 ms |
| 3 | 11 ms | 27 ms |
| 4 | 25 ms | 65 ms |
| 5 | 55 ms | 155 ms |
| 6 | 122 ms | 370 ms |
| 7 | 272 ms | 883 ms |
| 8 | 605 ms | 2.1 s |
| 9 | 1.35 s | 5.0 s |
| 10 | 3.0 s | 12.0 s |

Each slider position approximately doubles the time of the previous position. The midpoint (~position 5) falls around 55ms for attack and 155ms for decay/release, which are musically useful moderate times. This logarithmic distribution matches the feel of the real hardware, where the slider provides perceptually even control across its entire throw.

These values are approximate and should be validated against hardware measurements where possible.

---

## Envelope Curve Shape

### Attack Phase

The attack uses an RC-charge-toward-overshoot approach, consistent with the CEM3310 architecture. The capacitor charges exponentially toward a target voltage that is **higher** than the comparator threshold. When the voltage reaches the threshold, the attack phase ends and decay begins.

For the Juno-6 with a 7.5V reference:

- **Charge target**: ~10V (the overshoot target; this is the asymptotic voltage the RC would reach if uninterrupted)
- **Comparator threshold**: 7.5V (the reference voltage)
- **Overshoot ratio**: 10 / 7.5 ≈ 1.333

This produces a concave exponential curve that starts fast and decelerates as it approaches the peak — the classic analog synth attack shape.

```
// Normalized (0 to 1 output)
OVERSHOOT = 1.333

function attackCurve(t, attackTime):
    tau = attackTime / (-ln(1 - 1/OVERSHOOT))
    value = OVERSHOOT * (1 - exp(-t / tau))
    return min(value, 1.0)    // comparator clips at 1.0 (= 7.5V)
```

The time constant `tau` is derived so that the curve crosses 1.0 at exactly `t = attackTime`.

### Decay Phase

Standard exponential discharge from peak (1.0) toward the sustain level:

```
function decayCurve(t, decayTime, sustainLevel):
    tau = decayTime / 3.0     // 3 tau ≈ 95% of travel
    return sustainLevel + (1.0 - sustainLevel) * exp(-t / tau)
```

### Sustain Phase

Constant level held while gate is high. Sustain is a **level** parameter (0.0 to 1.0), not a time parameter.

### Release Phase

Standard exponential discharge from the current level toward zero:

```
function releaseCurve(t, releaseTime, startLevel):
    tau = releaseTime / 3.0
    return startLevel * exp(-t / tau)
```

### Retrigger Behavior

The Juno-6 has separate Gate and Trigger pins on the IR3R01. In typical operation, a new note-on restarts the attack from the current envelope level (not from zero). This should be implemented as:

- On note-on: begin attack phase from whatever level the envelope is currently at
- The attack charges toward the overshoot target from the current level
- If a note-on occurs during the release phase, the attack resumes from the release level

---

## Implementation Notes for VST

### Parameter Mapping

The pot taper (4.7kΩ shunt) and the IR3R01's internal exponential oscillator law approximately cancel each other out. The net result is a clean logarithmic mapping from slider position to time. For VST implementation, model this directly as a single exponential:

```
user_slider (0–1) → sliderToTime() → envelope time in seconds
```

There is no need to model the pot taper and oscillator law as separate stages — doing so double-compounds the curves and produces incorrect timing that concentrates far too much of the range at the fast end.

### Sample-Accurate Processing

For each audio sample:

1. Determine the current ADSR state (attack / decay / sustain / release / idle)
2. Compute the instantaneous envelope level using the formulas above
3. Advance internal time counter by `1 / sampleRate`
4. Handle state transitions (attack→decay when level reaches 1.0, etc.)

### Anti-Zipper

The pot taper and exponential mapping naturally smooth parameter changes, but you should still apply parameter smoothing (e.g., one-pole lowpass on the time parameters) to avoid clicks when automation moves the sliders.

### Polyphonic Considerations

On the real Juno-6, each voice has its own IR3R01 with a trimmer on the Common pin to match timing across voices. In a VST, all voices can share identical timing parameters — the trimmer matching is already "perfect" in software.

---

## DSP Pseudocode (Complete)

```
struct Juno6ADSR:
    state: enum { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE }
    level: float = 0.0
    time: float = 0.0

    // Constants
    OVERSHOOT = 1.333
    ATTACK_MIN = 0.001     // 1 ms
    ATTACK_MAX = 3.0       // 3 s
    DECAY_MIN = 0.002      // 2 ms
    DECAY_MAX = 12.0       // 12 s
    RELEASE_MIN = 0.002    // 2 ms
    RELEASE_MAX = 12.0     // 12 s

    function sliderToTime(slider, tMin, tMax):
        // Pot taper + oscillator exponential cancel out,
        // giving a clean log-linear mapping.
        // slider: 0.0 (fastest) to 1.0 (slowest)
        return tMin * (tMax / tMin) ^ slider

    function noteOn():
        state = ATTACK
        time = 0.0
        // level retains current value (retrigger from current level)

    function noteOff():
        state = RELEASE
        time = 0.0

    function process(sampleRate, atkSlider, decSlider, susSlider, relSlider):
        dt = 1.0 / sampleRate
        aTime = sliderToTime(atkSlider, ATTACK_MIN, ATTACK_MAX)
        dTime = sliderToTime(decSlider, DECAY_MIN, DECAY_MAX)
        rTime = sliderToTime(relSlider, RELEASE_MIN, RELEASE_MAX)
        sLevel = susSlider    // 0.0 to 1.0

        switch state:
            case ATTACK:
                tau = aTime / (-ln(1 - 1/OVERSHOOT))
                target = OVERSHOOT
                level = target + (level - target) * exp(-dt / tau)
                if level >= 1.0:
                    level = 1.0
                    state = DECAY
                    time = 0.0

            case DECAY:
                tau = dTime / 3.0
                level = sLevel + (level - sLevel) * exp(-dt / tau)
                if abs(level - sLevel) < 0.0001:
                    level = sLevel
                    state = SUSTAIN

            case SUSTAIN:
                level = sLevel

            case RELEASE:
                tau = rTime / 3.0
                level = level * exp(-dt / tau)
                if level < 0.0001:
                    level = 0.0
                    state = IDLE

            case IDLE:
                level = 0.0

        return level
```

---

## GATE Mode (VCA Switch)

The Juno-6's VCA section has a three-position switch: **ENV / GATE / OFF**. In ENV mode the ADSR output controls the VCA. In GATE mode the ADSR is bypassed entirely and the gate signal drives the VCA more or less directly.

### Hardware Behavior

The gate signal itself is a simple digital high/low from the CPU voice assignment circuit — no slew or shaping is applied to it. However, the Juno-6's VCA (a Roland BA662 OTA-based design) has inherent bandwidth limitations on its control input. The parasitic capacitance on the control port introduces a very short natural slew, on the order of 0.5–2ms, that slightly rounds the leading and trailing edges of the gate.

This means GATE mode is not a mathematically perfect step function, but it is *nearly* instantaneous — much faster than even the minimum ADSR attack time. The result is a punchy, percussive, slightly clicky character that is distinct from ENV mode. The residual click is part of the Juno-6's sonic identity in GATE mode.

### Gate Signal Chain

The IR3R01 has three gate-related input pins:

| Pin | Name | Function |
|---|---|---|
| 13 | Gate | High while key held. Rising edge → attack. Falling edge → release. |
| 14 | Trigger | Pulse on note-on. Resets attack phase. |
| 15 | Retrigger | Pulse on legato note change (new key while gate still high). |

On the Juno-6, Trigger and Gate are effectively tied together — every note-on restarts the attack from the current envelope level. The Retrigger pin is more relevant to the Jupiter-8's voice assignment modes and can be ignored for a Juno-6 emulation.

### DSP Implementation

Do not implement GATE mode as a hard sample-to-sample switch (0→1 in one sample). This will produce broadband digital clicks far harsher than the analog original. Instead, model the BA662 control port slew with a very short exponential ramp:

```
struct GateMode:
    level: float = 0.0
    TAU = 0.001    // ~1ms — approximates BA662 control port response

    function process(sampleRate, gateIsOn):
        dt = 1.0 / sampleRate
        target = 1.0 if gateIsOn else 0.0
        level = target + (level - target) * exp(-dt / TAU)
        return level
```

The 1ms time constant is a reasonable starting point. For more precise emulation, this could be tuned against hardware measurements. Values in the range of 0.5–2ms will all sound plausibly correct.

### OFF Mode

When the VCA switch is in the OFF position, the VCA is fully open (signal passes through at unity gain regardless of gate or envelope state). This is useful for drone patches and for hearing the raw oscillator output.

---

## Sources

1. **AMSynths — "Roland IR3R01 Design & Replica"**
   https://amsynths.co.uk/2019/05/09/roland-ir3r01-clone/
   Primary source for IR3R01 pin-out, CV ranges, timing capacitor values, reference voltages, maximum timing values per synth model, pot taper resistor values (4.7kΩ shunt), and the reverse-log taper explanation. Also covers the relationship to the Jupiter-4 CMOS oscillator ADSR and the modulation pin.

2. **AMSynths — "AM8430 JP-4 ADSR"**
   https://amsynths.co.uk/home/products/modulators/am8108-jp04-dual-adsr-vca/
   Detailed analysis of the Jupiter-4 ADSR architecture (CMOS oscillator-driven timing) which the IR3R01 descends from. Covers oscillator frequency ranges (10kHz–1MHz for attack, higher for D/R), the 150pF/47pF timing cap differences, the 50kΩ pots with 22kΩ reverse-taper resistors, and the step-based charge/discharge mechanism.

3. **Electric Druid — "Voltage Controlled ADSR Envelope Generator (VC ADSR 7B)"**
   https://electricdruid.net/voltage-controlled-adsr-envelope-generator-vc-adsr-7b/
   Discussion of IR3R01 replacement strategies, ENVGEN8 PIC-based replica approach, and real-world Juno-60 envelope curve measurements (described as accurately exponential).

4. **Mod Wiggler / Muff Wiggler — "Roland IR3R01 Envelope Generator ICs - testing"**
   https://www.modwiggler.com/forum/viewtopic.php?p=1368035
   Community testing of IR3R01 on breadboard, 0–5V CV input confirmed for ADSR, pot wiring recommendations.

5. **EarLevel Engineering — "Envelope generators—ADSR video"**
   https://www.earlevel.com/main/2013/06/22/envelope-generators-adsr-video/
   General analysis of analog ADSR curve shapes including the CEM3310-style overshoot attack (charge toward a higher target, comparator trips at the desired level), variable-curvature decay/release via current sinks, and the exponential-vs-linear decay distinction. Applicable to the IR3R01's similar architecture.

---

## Caveats

- The IR3R01 is a custom Roland chip with no published datasheet. All parameters are derived from schematic analysis, service manual notes, community reverse-engineering, and comparison with known similar architectures (CEM3310, Jupiter-4 CMOS ADSR).
- The exact internal architecture of the IR3R01 is still debated. The model above uses a CEM3310-like RC overshoot attack and exponential decay/release, which is consistent with available measurements and the "accurately exponential" curves reported from Juno-60 oscilloscope captures.
- The overshoot ratio of 1.333 (10V target / 7.5V reference) is inferred from the circuit topology. Real-world measurements may show slight variation.
- The slider-to-time mapping assumes perfect cancellation between the pot taper and the oscillator exponential law. In practice the cancellation is approximate — the 4.7kΩ shunt produces a reverse-log curve that is close to but not exactly the inverse of the exponential oscillator response. The simple `sliderToTime()` exponential model is a good first approximation, but hardware measurements may reveal slight deviations, particularly at the extremes of the slider throw. If measurements become available, the `sliderToTime` function should be calibrated against them.
- The minimum time values (1ms attack, 2ms D/R) are estimates. The real minimum depends on the internal oscillator maximum frequency and may be slightly different.
