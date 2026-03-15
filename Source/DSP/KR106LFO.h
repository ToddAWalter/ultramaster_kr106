#pragma once

#include <cmath>
#include <algorithm>
#include "KR106ADSR.h" // for ADSR::AttackIncFromSlider

// Global triangle LFO with delayed onset
// Ported from kr106_lfo.h/kr106_lfo.C
//
// Juno-6:   RC exponential delay envelope (eases in at top, capacitor curve)
// Juno-106: Two-stage digital envelope from D7811G firmware:
//           1. Holdoff — silent period, duration from attack table
//           2. Ramp    — linear fade-in, rate from 8-entry LFO delay ramp table
//
// Improvements over naive version:
// - Rounded triangle (soft-clipped peaks, matching capacitor charge curve)
// - Free-running in auto mode (delay envelope persists across legato notes)

namespace kr106
{

struct LFO
{
  float mPos        = 0.f; // phase [0, 1)
  float mFreq       = 0.f; // cycles per sample
  float mAmp        = 0.f; // current amplitude envelope [0, 1]
  float mLastTri    = 0.f; // raw triangle waveform [-1,+1] before envelope
  float mDelayCoeff = 0.f; // J6: RC envelope coefficient (0 = instant)
  float mDelayParam = 0.f; // J6: tau in seconds
  float mSampleRate = 44100.f;
  bool mActive      = false; // any voice currently gated?
  bool mWasActive   = false;
  int mMode         = 0;     // 0=auto, 1=manual
  bool mTrigger     = false; // manual trigger state
  bool mJ6Mode      = false; // true = Juno-6, false = Juno-106

  // --- J106 two-stage delay state ---
  float mHoldoffRemaining = 0.f; // samples remaining in holdoff phase
  float mRampPerSample    = 0.f; // depth increment per sample during ramp
  bool  mInHoldoff        = false;
  float mSlider           = 0.f; // stored slider value for reset

  float lfoFreq(float t) { return mJ6Mode ? lfoFreqJ6(t) : lfoFreqJ106(t); }

  // FIXME(kr106) Measure LFO frq compared to slider voltage on hardware Juno 6
  static float lfoFreqJ6(float t) {
    // linear mapping from our original 2001 codebase
    return ( ( 18.f + t * 1182.f ) / 60.f );
  }

  // Maps normalized slider 0..1 to LFO frequency in Hz
  // Derived from curve analysis of Roland Juno-106 voice firmware timing.
  // Frame rate 238.1 Hz (1/4.2ms), LFO accumulator full cycle = 16384 steps.
  static float lfoFreqJ106(float t)
  {
    float i = t * 127.f;
    float coeff;
    if (i < 96.f)
      coeff = 5.f + 12.18f * i; // linear region
    else
      coeff = 1214.f * powf(1.039f, i - 96.f); // exponential region

    // freq = coeff * frameRate / cyclePeriod
    //      = coeff * 238.1 / 16384
    return coeff / 68.81f;
  }

  void SetRate(float slider, float sampleRate)
  {
    mSampleRate = sampleRate;
    mFreq       = lfoFreq(slider) / sampleRate;
  }

  // FIXME(kr106) Measure LFO delay vs slider voltage on hardware Juno-6
  // Returns tau in seconds for RC exponential envelope.
  static float lfoDelayJ6(float t) { return t * 1.5f; }

  // --- Juno-106 LFO delay: two-stage envelope from D7811G firmware ---
  //
  // Stage 1 — Holdoff: accumulator += attackTable[pot] per tick (238.1 Hz).
  //   Completes when accumulator >= 0x4000. LFO depth = 0 during this phase.
  //   Uses the same attack rate table as the ADSR (0B60_envAtkTbl).
  //
  // Stage 2 — Ramp: accumulator += rampTable[pot>>4] per tick.
  //   LFO depth = high byte of 16-bit accumulator / 255.
  //   Linear fade-in until 16-bit overflow, then clamp to full depth.
  //   Ramp table (0B30_LfoDelayRampTbl): 8 entries indexed by pot >> 4.

  static constexpr float kTickRate = 238.1f; // D7811G main loop rate (1/4.2ms)

  // Clean-room LFO delay ramp table (0B30_LfoDelayRampTbl).
  // 8 entries, indexed by (pot >> 4). Larger value = faster ramp.
  static constexpr uint16_t kLfoRampTable[8] = {
    0xFFFF, // pot 0-15:   instant ramp
    0x0419, // pot 16-31:  fast
    0x020C, // pot 32-47
    0x015E, // pot 48-63
    0x0100, // pot 64-79:  slow
    0x0100, // pot 80-95:  (same)
    0x0100, // pot 96-111: (same)
    0x0100  // pot 112-127:(same)
  };

  // Compute holdoff duration in seconds for J106 LFO delay.
  // Uses the same clean-room attack rate function as the ADSR.
  static float lfoHoldoffSeconds106(float slider)
  {
    uint16_t inc = ADSR::AttackIncFromSlider(slider);
    if (inc >= ADSR::kEnvMax) return 0.f; // instant
    // ticks to reach 0x4000: accumulator >= 0x4000 after ceil(0x4000/inc) ticks
    float ticks = static_cast<float>(ADSR::kEnvMax) / static_cast<float>(inc);
    return ticks / kTickRate;
  }

  // Compute ramp rate (depth per second) for J106 LFO delay.
  // Depth = high byte of 16-bit accumulator / 255, so full scale at overflow.
  // Rate per tick = rampTable[idx] / 65536 (normalized 0..1).
  // Rate per second = rate_per_tick * tickRate.
  static float lfoRampPerSecond106(float slider)
  {
    int pot = static_cast<int>(slider * 127.f + 0.5f);
    int idx = std::clamp(pot >> 4, 0, 7);
    uint16_t rampInc = kLfoRampTable[idx];
    if (rampInc == 0xFFFF) return 1e6f; // instant
    return (static_cast<float>(rampInc) / 65536.f) * kTickRate;
  }

  void SetDelay(float slider)
  {
    mSlider = slider;
    if (mJ6Mode)
    {
      mDelayParam = lfoDelayJ6(slider);
      RecalcDelayJ6();
    }
    else
    {
      RecalcDelay106();
    }
  }

  void SetMode(int mode) { mMode = mode; }
  void SetTrigger(bool trig) { mTrigger = trig; }
  void SetVoiceActive(bool active) { mActive = active; }

  // Process one sample, returns [-1, +1]
  float Process()
  {
    bool newState = (mMode == 0) ? mActive : mTrigger;

    if (newState && !mWasActive)
    {
      // Auto mode: only reset envelope on first note (not legato)
      // Manual mode: always reset on trigger
      if (mMode == 1 || mAmp <= 0.f)
      {
        mAmp = 0.f;
        if (mJ6Mode)
          RecalcDelayJ6();
        else
          RecalcDelay106();
      }
    }

    if (!newState && mWasActive && mMode == 1)
      mAmp = 0.f; // manual mode: cut immediately on release

    mWasActive = newState;

    mPos += mFreq;
    if (mPos >= 1.f)
      mPos -= 1.f;

    if (newState && mAmp < 1.f)
    {
      if (mJ6Mode)
      {
        // J6: RC exponential envelope approaching 1.0 asymptotically
        if (mDelayCoeff <= 0.f)
          mAmp = 1.f; // instant
        else
          mAmp += mDelayCoeff * (1.f - mAmp);
      }
      else
      {
        // J106: holdoff (silent) then linear ramp
        if (mInHoldoff)
        {
          mHoldoffRemaining -= 1.f;
          if (mHoldoffRemaining <= 0.f)
            mInHoldoff = false;
          // mAmp stays 0 during holdoff
        }
        else
        {
          mAmp += mRampPerSample;
          if (mAmp >= 1.f) mAmp = 1.f;
        }
      }
    }

    // Rounded triangle: linear triangle with soft-clipped peaks
    // Linear triangle: +1 at pos=0, -1 at pos=0.5, +1 at pos=1
    float tri = 1.f - 4.f * fabsf(mPos - 0.5f);
    // Soft-clip peaks using cubic saturation (matches capacitor rounding)
    tri = tri * (1.5f - 0.5f * tri * tri);

    mLastTri = tri; // raw waveform before envelope (for J106 integer VCF path)
    return tri * mAmp;
  }

private:
  void RecalcDelayJ6()
  {
    if (mDelayParam <= 0.f)
    {
      mDelayCoeff = 0.f; // instant (handled as special case)
    }
    else
    {
      // mDelayParam is tau in seconds (from lfoDelayJ6)
      mDelayCoeff = 1.f - expf(-1.f / (mDelayParam * mSampleRate));
    }
  }

  void RecalcDelay106()
  {
    float holdoff = lfoHoldoffSeconds106(mSlider);
    mHoldoffRemaining = holdoff * mSampleRate;
    mInHoldoff = (mHoldoffRemaining > 0.f);
    float rampPerSec = lfoRampPerSecond106(mSlider);
    mRampPerSample = rampPerSec / mSampleRate;
  }
};

} // namespace kr106
