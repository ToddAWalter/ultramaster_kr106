#!/usr/bin/env python3
"""Convert Juno-60 tape dump CSV (0-255 raw bytes) to KR-106 preset format.

The tape dump stores raw 8-bit ADC values. We convert to 7-bit (0-127)
by dividing by 2 (equivalent to taking the top 7 bits), matching how the
J106 firmware quantizes the same analog circuits.

Special cases:
  - LFO Delay: inverted (255=no delay, 18=max delay)
  - ENV Attack: range 28-255 (fastest attack is byte 28, not 0)
  - VCA Level: 128=center (unity), 0=max cut, 255=max boost

Input:  juno60_group1.csv, juno60_group2.csv (tape dump format)
Output: Combined 256-preset KR106_Presets_JUCE.h (128 J6 + 128 J106)
"""

import csv
import sys
import os

def convert_tape_row(row, oct_transpose=1):
    """Convert one tape dump CSV row to 44-element preset array.
    oct_transpose: 0=16', 1=8', 2=4' (from manual, not in tape data)."""
    vals = [0] * 44

    def remap(v, tape_min, tape_max):
        """Remap tape byte range to 0-127, clamping to observed range."""
        v = v.strip()
        if not v:
            return 0
        raw = int(v)
        if tape_max == tape_min:
            return 0
        normalized = max(0.0, min(1.0, (raw - tape_min) / (tape_max - tape_min)))
        return min(127, round(normalized * 127))

    def remap_inverted(v, tape_min, tape_max):
        """Remap inverted tape byte (high=0, low=max) to 0-127."""
        v = v.strip()
        if not v:
            return 0
        raw = int(v)
        if tape_max == tape_min:
            return 0
        normalized = max(0.0, min(1.0, (tape_max - raw) / (tape_max - tape_min)))
        return min(127, round(normalized * 127))

    # Sliders -- remap from tape 0-255 to 0-127
    # Most sliders: straight 0-255 -> 0-127
    # Two exceptions from hardware analysis:
    #   ENV Attack: range 28-255 (circuit can't reach 0)
    #   LFO Delay: inverted, range 255 (no delay) to 18 (max delay)
    vals[3]  = remap(row['LFO Rate'],       0, 255)
    vals[4]  = remap_inverted(row['LFO Delay'], 18, 255) # inverted, min 18
    vals[5]  = remap(row['DCO LFO'],        0, 255)
    vals[6]  = remap(row['DCO PWM'],        0, 255)
    vals[7]  = remap(row['DCO Sub Osc'],    0, 255)
    vals[8]  = remap(row['DCO Noise'],      0, 255)
    # HPF: tape format swaps bass boost and flat vs J106 convention
    # tape 0 = Flat (J106 position 1)
    # tape 1 = Bass Boost (J106 position 0)
    # tape 2 = 240 Hz (J106 position 2)
    # tape 3 = 720 Hz (J106 position 3)
    # Confirmed by comparing 56 tape patches against manual values.
    hpf_map = {0: 1, 1: 0, 2: 2, 3: 3}
    hpf_raw = int(row['HPF'].strip() or '0')
    vals[9]  = hpf_map.get(hpf_raw, hpf_raw)
    vals[10] = remap(row['VCF Freq'],       0, 255)
    vals[11] = remap(row['VCF Res'],        0, 255)
    vals[12] = remap(row['VCF Env'],        0, 255)
    vals[13] = remap(row['VCF LFO'],        0, 255)
    vals[14] = remap(row['VCF KYBD'],       0, 255)
    vals[15] = remap(row['VCA Level'],      0, 255)
    vals[16] = remap(row['ENV Attack'],    28, 255)    # hardware min 28
    vals[17] = remap(row['ENV Decay'],      0, 255)
    vals[18] = remap(row['ENV Sustain'],    0, 255)
    vals[19] = remap(row['ENV Release'],    0, 255)

    # Switches (0/1/2 integers, pass through)
    vals[23] = int(row['Square Wave'])       # kDcoPulse
    vals[24] = int(row['Triangle Wave'])     # kDcoSaw
    vals[25] = int(row['Sub Wave'])          # kDcoSubSw
    vals[26] = int(row['Chorus Off'])        # kChorusOff
    vals[27] = int(row['Chorus I'])          # kChorusI
    vals[28] = int(row['Chorus II'])         # kChorusII
    vals[29] = oct_transpose                  # kOctTranspose from manual (not in tape)
    vals[32] = int(row['LFO Trigger'])       # kLfoMode
    vals[33] = int(row['DCO PWM Mode'])      # kPwmMode
    vals[34] = int(row['VCF Polarity'])      # kVcfEnvInv
    # VCA Type: tape 0=Gate, 1=Env; our kVcaMode 0=Env, 1=Gate (inverted)
    vals[35] = 1 - int(row['VCA Type'].strip() or '0')  # kVcaMode
    vals[38] = 1                             # kPower
    vals[39] = 2                             # kPortaMode (Poly II)
    vals[43] = 0                             # kAdsrMode (J6)

    return vals

def load_tape_group(filename, oct_map=None):
    """Load a tape dump CSV and return list of (group, patch, vals) tuples.
    oct_map: dict of 'G-P' -> oct_transpose value (from manual)."""
    presets = []
    with open(filename) as f:
        for row in csv.DictReader(f):
            num = row['Number'].strip()  # e.g. "1-1"
            group = int(num.split('-')[0])
            patch = int(num.split('-')[1])
            oct = oct_map.get(num, 1) if oct_map else 1  # default 8'
            vals = convert_tape_row(row, oct_transpose=oct)
            presets.append((group, patch, vals))
    return presets

def make_init_preset(adsr_mode=0):
    """Return 44-value array for an Init preset."""
    vals = [0] * 44
    vals[15] = 64   # VCA Level center
    vals[18] = 127  # Sustain max
    vals[24] = 1    # Saw on
    vals[38] = 1    # Power on
    vals[39] = 2    # Porta Mode Poly II
    vals[43] = adsr_mode
    return vals

def main():
    toolsdir = os.path.dirname(os.path.abspath(__file__))
    group1 = os.path.join(toolsdir, 'juno60_group1.csv')
    group2 = os.path.join(toolsdir, 'juno60_group2.csv')
    j106file = os.path.join(toolsdir, '..', 'Source', 'KR106_Presets_JUCE.h')
    outfile = j106file

    # Load octave transpose from manual (Bank A only; not stored in tape)
    # U=Up=4'(2), N=Normal=8'(1), D=Down=16'(0)
    oct_map_a = {}
    manual_file = os.path.join(toolsdir, 'juno60_patch_data.csv')
    if os.path.exists(manual_file):
        with open(manual_file) as f:
            for row in csv.DictReader(f):
                p = row['Patch'].strip()
                a = row['Assign/Range'].strip()
                key = f"{p[0]}-{p[1]}"
                oct_map_a[key] = 2 if a == 'U' else (0 if a == 'D' else 1)

    # Load tape dumps
    g1 = load_tape_group(group1, oct_map=oct_map_a)
    g2 = load_tape_group(group2)  # Bank B: no manual octave data, default 8'
    print(f"Group 1 (Bank A): {len(g1)} patches")
    print(f"Group 2 (Bank B): {len(g2)} patches")

    # Load names for Bank A (from manual)
    bank_a_names = {}
    manual_file = os.path.join(toolsdir, 'juno60_patch_data.csv')
    if os.path.exists(manual_file):
        with open(manual_file) as f:
            for row in csv.DictReader(f):
                p = row['Patch'].strip()
                bank_a_names[p] = row['Name'].strip()

    # Load names for Bank B
    bank_b_names = {}
    bank_b_file = os.path.join(toolsdir, 'juno60_bank_b_names.csv')
    if os.path.exists(bank_b_file):
        with open(bank_b_file) as f:
            for row in csv.DictReader(f):
                p = row['Patch'].strip()
                bank_b_names[p] = row['Name'].strip()

    # Build J6 bank: 128 slots
    # Slots 0-55 = Bank A (group1), slots 56-63 = Init
    # Slots 64-119 = Bank B (group2), slots 120-127 = Init
    j6_presets = []

    # Bank A: group1 into slots 0-55
    tape_a = {}
    for group, patch, vals in g1:
        idx = (group - 1) * 8 + (patch - 1)
        tape_a[idx] = vals

    # Bank B: group2 into slots 64-119
    tape_b = {}
    for group, patch, vals in g2:
        idx = 64 + (group - 1) * 8 + (patch - 1)
        tape_b[idx] = vals

    for i in range(128):
        prefix = 'A' if i < 64 else 'B'
        gi = (i % 64) // 8 + 1
        pi = i % 8 + 1
        patch_num = f"{gi}{pi}"

        if i in tape_a:
            name_str = bank_a_names.get(patch_num, f"Patch {gi}-{pi}")
            name = f"{prefix}{gi}{pi} {name_str}"
            vals = tape_a[i]
        elif i in tape_b:
            name_str = bank_b_names.get(patch_num, f"Patch {gi}-{pi}")
            name = f"{prefix}{gi}{pi} {name_str}"
            vals = tape_b[i]
        else:
            name = f"{prefix}{gi}{pi} Init"
            vals = make_init_preset(adsr_mode=0)
        j6_presets.append((name, vals))

    # Read existing J106 presets
    j106_presets = []
    with open(j106file) as f:
        in_j106 = False
        for line in f:
            if 'J106 Bank' in line:
                in_j106 = True
                continue
            if in_j106 and line.strip().startswith('{"'):
                pname = line.split('"')[1]
                vals_str = line.split('{', 2)[2].split('}')[0]
                pvals = [int(x.strip()) for x in vals_str.split(',')]
                j106_presets.append((pname, pvals))

    print(f"J106 presets: {len(j106_presets)}")

    # Write combined header
    with open(outfile, 'w') as f:
        f.write('#pragma once\n\n')
        f.write('// Auto-generated -- 256 factory presets (128 J6 + 128 J106).\n')
        f.write('// J6 presets decoded from Juno-60 tape dump (raw 8-bit -> 7-bit).\n')
        f.write('// J106 presets decoded from factory SysEx data.\n\n')
        f.write('struct KR106FactoryPreset {\n')
        f.write('    const char* name;\n')
        f.write('    int values[44];\n')
        f.write('};\n\n')
        f.write('static constexpr int kNumFactoryPresets = 256;\n\n')
        f.write('static const KR106FactoryPreset kFactoryPresets[] = {\n')
        f.write('    // === J6 Bank (0-127): Juno-60 tape dump patches ===\n')
        for name, vals in j6_presets:
            f.write(f'    {{"{name}", {{{", ".join(str(v) for v in vals)}}}}},\n')
        f.write('    // === J106 Bank (128-255): Juno-106 factory patches ===\n')
        for name, vals in j106_presets:
            f.write(f'    {{"{name}", {{{", ".join(str(v) for v in vals)}}}}},\n')
        f.write('};\n')

    print(f"Wrote {len(j6_presets) + len(j106_presets)} presets to {outfile}")

if __name__ == '__main__':
    main()
