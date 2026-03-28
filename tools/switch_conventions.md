# Switch Parameter Conventions

Three formats with different bit conventions for the same parameters.

| Parameter | Internal (kParam) | J106 SysEx (sw1/sw2) | J60 Tape (sw1/sw2) | Conversion needed |
|-----------|-------------------|----------------------|--------------------|--------------------|
| DCO Octave | 0=16', 1=8', 2=4' | sw1 b0-2: 01=16', 02=8', 04=4' | N/A (physical switch, not stored) | J106 SysEx uses one-hot encoding |
| VCF Env Polarity | 0=Normal, 1=Inverted | sw2 b1: 0=+, 1=- | sw1 B0: 0=Normal, 1=Inverted | None (all match) |
| PWM Mode | 0=LFO, 1=Manual, 2=Env | sw2 b0: 0=LFO, 1=Manual | sw1 B1-B2: 0=LFO, 1=Manual, 2=Env | J106 SysEx only 1 bit (no Env) |
| VCA Mode | 0=Env, 1=Gate | sw2 b2: 0=ENV, 1=GATE | sw1 B5: 0=Gate, 1=Env | J60 tape INVERTED vs Internal/J106 |
| LFO Trigger | 0=Free, 1=Triggered | N/A (not in SysEx) | sw1 B7: 0=Auto, 1=Manual | None (match) |
| HPF | 0=BassBoost, 1=Flat, 2=240Hz, 3=720Hz | sw2 b3-4: 00=720Hz, 01=240Hz, 10=Flat, 11=BassBoost | sw1 B3-B4: 0=Flat, 1=BassBoost, 2=240Hz, 3=720Hz | J60 tape: swap 0/1. J106 SysEx: bit-reversed. |
| Pulse (Square) | 0=Off, 1=On | sw1 b3: 1=On | sw2 B1: 0=Off, 1=On | None (all match) |
| Saw (Triangle) | 0=Off, 1=On | sw1 b4: 1=On | sw2 B2: 0=Off, 1=On | None (all match) |
| Sub Wave | 0=Off, 1=On | N/A (Sub level slider only) | sw2 B3: 0=Off, 1=On | None |
| Chorus I | 0=Off, 1=On | sw1 b5=0(on) + b6=1(I) | sw2 B0: 0=Off, 1=On | J106 SysEx uses combined on/off + level bits |
| Chorus II | 0=Off, 1=On | sw1 b5=0(on) + b6=0(II) | sw2 B4: 0=Off, 1=On | J106 SysEx uses combined on/off + level bits |
| Chorus Off | 0=Off, 1=On | sw1 b5: 0=on, 1=off | sw2 B5: 0=On, 1=Off | Both J106/J60 invert vs Internal |

## Verified against manual (56 patches)

- VCF Polarity: 0 mismatches
- PWM Mode: 2 mismatches (tape says Env, manual says Manual on patches 53, 56)
- Pulse: 0 mismatches
- Saw/Triangle: 0 mismatches
- Sub: 0 mismatches
- HPF: 2 mismatches (tape vs manual, likely real differences on this unit)
- VCA Mode: not directly verified (inversion discovered via A14 Organ 1)

## Key inversions to remember

- **VCA Mode**: J60 tape is opposite of Internal and J106 SysEx
- **HPF**: J60 tape swaps positions 0 (BassBoost) and 1 (Flat) vs Internal
- **Chorus Off**: Both external formats use 0=On, Internal uses 0=Off
- **DCO Octave**: Not in J60 tape. J106 SysEx uses one-hot (not 0/1/2). We default to 8' for J60 patches but override from manual data where available.
