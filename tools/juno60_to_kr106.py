#!/usr/bin/env python3
"""Convert Juno-60 manual patch data (CSV) to KR-106 preset CSV format.

Output format matches KR106PresetManager CSV:
- Header row with param names (excluding live performance params)
- Slider values as [0-127] (MIDI format)
- Switch/bool values as [int]
- adsrMode = 0 (J6 mode) for all patches
"""

import csv
import sys

# Live performance params (excluded from CSV, matching PluginProcessor.cpp)
LIVE_PARAMS = {
    'Bender DCO', 'Bender VCF', 'Bender LFO', 'Arp Rate',
    'Transpose', 'Hold', 'Arpeggio', 'Arp Mode', 'Arp Range',
    'Bender', 'Tuning', 'Power', 'Porta Mode', 'Porta Rate',
    'Transpose Offset', 'Master Volume'
}

# Params in order, with their type and name as the plugin sees them
# (idx, name, type, range_note)
PARAMS = [
    (0,  'Bender DCO',      'slider'),
    (1,  'Bender VCF',      'slider'),
    (2,  'Arp Rate',        'slider'),
    (3,  'LFO Rate',        'slider'),
    (4,  'LFO Delay',       'slider'),
    (5,  'DCO LFO',         'slider'),
    (6,  'DCO PWM',         'slider'),
    (7,  'DCO Sub',         'slider'),
    (8,  'DCO Noise',       'slider'),
    (9,  'HPF',             'switch'),  # 4-position: 0-3
    (10, 'VCF Freq',        'slider'),
    (11, 'VCF Res',         'slider'),
    (12, 'VCF Env',         'slider'),
    (13, 'VCF LFO',         'slider'),
    (14, 'VCF Kbd',         'slider'),
    (15, 'Volume',          'slider'),
    (16, 'Attack',          'slider'),
    (17, 'Decay',           'slider'),
    (18, 'Sustain',         'slider'),
    (19, 'Release',         'slider'),
    (20, 'Transpose',       'bool'),
    (21, 'Hold',            'bool'),
    (22, 'Arpeggio',        'bool'),
    (23, 'Pulse',           'bool'),
    (24, 'Saw',             'bool'),
    (25, 'Sub Sw',          'bool'),
    (26, 'Chorus Off',      'bool'),
    (27, 'Chorus I',        'bool'),
    (28, 'Chorus II',       'bool'),
    (29, 'Octave',          'switch'),
    (30, 'Arp Mode',        'switch'),
    (31, 'Arp Range',       'switch'),
    (32, 'LFO Mode',        'switch'),
    (33, 'PWM Mode',        'switch'),
    (34, 'VCF Env Inv',     'switch'),
    (35, 'VCA Mode',        'switch'),
    (36, 'Bender',          'slider'),
    (37, 'Tuning',          'slider'),
    (38, 'Power',           'bool'),
    (39, 'Porta Mode',      'switch'),
    (40, 'Porta Rate',      'slider'),
    (41, 'Transpose Offset','switch'),
    (42, 'Bender LFO',      'slider'),
    (43, 'ADSR Mode',       'switch'),
    (44, 'Master Volume',   'slider'),
]

def slider_0_10(val_str):
    """Convert 0-10 slider to 0-127."""
    if not val_str or not val_str.strip():
        return 0
    return round(float(val_str.strip()) / 10.0 * 127)

def hpf_switch(val_str):
    """HPF 4-position switch: 0-3 integer."""
    if not val_str or not val_str.strip():
        return 0
    return int(float(val_str.strip()))

def vca_level(val_str):
    """Convert -5..+5 to 0-127."""
    if not val_str or not val_str.strip():
        return 64
    return round((float(val_str.strip()) + 5.0) / 10.0 * 127)

def convert_row(row):
    """Convert one CSV row to dict of {param_name: value}."""
    vals = {}

    # Sliders (0-1 range, stored as [midi])
    vals['LFO Rate']   = slider_0_10(row['LFO_Rate'])
    vals['LFO Delay']  = slider_0_10(row['LFO_Delay'])
    vals['DCO LFO']    = slider_0_10(row['DCO_LFO'])
    vals['DCO PWM']    = slider_0_10(row['DCO_PWM'])
    vals['DCO Sub']    = slider_0_10(row['DCO_Sub_Level'])
    vals['DCO Noise']  = slider_0_10(row['DCO_Noise'])
    vals['HPF']        = hpf_switch(row['HPF_Freq'])
    vals['VCF Freq']   = slider_0_10(row['VCF_Freq'])
    vals['VCF Res']    = slider_0_10(row['VCF_Res'])
    vals['VCF Env']    = slider_0_10(row['VCF_Env_Amt'])
    vals['VCF LFO']    = slider_0_10(row['VCF_LFO_Amt'])
    vals['VCF Kbd']    = slider_0_10(row['VCF_Kybd_Amt'])
    vals['Volume']     = vca_level(row['VCA_Level'])
    vals['Attack']     = slider_0_10(row['ENV_A'])
    vals['Decay']      = slider_0_10(row['ENV_D'])
    vals['Sustain']    = slider_0_10(row['ENV_S'])
    vals['Release']    = slider_0_10(row['ENV_R'])

    # Switches/bools
    vals['Pulse']      = 1 if row['DCO_Square'].strip() == '*' else 0
    vals['Saw']        = 1 if row['DCO_Saw'].strip() == '*' else 0
    vals['Sub Sw']     = 1 if row['DCO_Sub'].strip() == '*' else 0
    vals['Chorus Off'] = 1 if row['Chorus_Off'].strip() == '*' else 0
    vals['Chorus I']   = 1 if row['Chorus_1'].strip() == '*' else 0
    vals['Chorus II']  = 1 if row['Chorus_2'].strip() == '*' else 0

    # U=Up=4'(2), N=Normal=8'(1), D=Down=16'(0)
    assign = row['Assign/Range'].strip()
    vals['Octave'] = 2 if assign == 'U' else (0 if assign == 'D' else 1)

    # A=0(free), M=1(triggered)
    vals['LFO Mode'] = 1 if row['LFO_Trig'].strip() == 'M' else 0

    # L=0, M=1, E=2
    pwm = row['DCO_PWM_Mode'].strip()
    vals['PWM Mode'] = 1 if pwm == 'M' else (2 if pwm == 'E' else 0)

    # N=0, I=1
    vals['VCF Env Inv'] = 1 if row['VCF_Env_Pol'].strip() == 'I' else 0

    # E=0, G=1
    vals['VCA Mode'] = 1 if row['VCA_Mode'].strip() == 'G' else 0

    # J6 mode
    vals['ADSR Mode'] = 0

    return vals

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
    infile = sys.argv[1] if len(sys.argv) > 1 else 'tools/juno60_patch_data.csv'
    outfile = sys.argv[2] if len(sys.argv) > 2 else 'tools/juno60_presets.csv'

    with open(infile) as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    # Build header: name + non-excluded param names
    included = [(idx, name, typ) for idx, name, typ in PARAMS if name not in LIVE_PARAMS]
    header = 'name,' + ','.join(name for _, name, _ in included)

    lines = [header]
    for row in rows:
        patch = row['Patch'].strip()
        name = row['Name'].strip()
        label = name
        vals = convert_row(row)

        fields = [label]
        for idx, pname, typ in included:
            if pname not in vals:
                if typ == 'slider' or typ == 'slider_hpf':
                    fields.append('[0]')
                else:
                    fields.append('[0]')
                continue

            v = vals[pname]
            if typ == 'slider':
                fields.append(f'[{int(v)}]')
            elif typ in ('bool', 'switch'):
                fields.append(f'[{int(v)}]')

        lines.append(','.join(fields))

    with open(outfile, 'w') as f:
        f.write('\n'.join(lines) + '\n')

    print(f"Wrote {len(rows)} presets to {outfile}")

def convert_row_flat(row):
    """Convert one CSV row to flat 44-element array (C++ format)."""
    v = convert_row(row)
    arr = [0] * 44
    # Sliders
    arr[3]  = v.get('LFO Rate', 0)
    arr[4]  = v.get('LFO Delay', 0)
    arr[5]  = v.get('DCO LFO', 0)
    arr[6]  = v.get('DCO PWM', 0)
    arr[7]  = v.get('DCO Sub', 0)
    arr[8]  = v.get('DCO Noise', 0)
    arr[9]  = v.get('HPF', 0)
    arr[10] = v.get('VCF Freq', 0)
    arr[11] = v.get('VCF Res', 0)
    arr[12] = v.get('VCF Env', 0)
    arr[13] = v.get('VCF LFO', 0)
    arr[14] = v.get('VCF Kbd', 0)
    arr[15] = v.get('Volume', 64)
    arr[16] = v.get('Attack', 0)
    arr[17] = v.get('Decay', 0)
    arr[18] = v.get('Sustain', 0)
    arr[19] = v.get('Release', 0)
    # Switches
    arr[23] = v.get('Pulse', 0)
    arr[24] = v.get('Saw', 0)
    arr[25] = v.get('Sub Sw', 0)
    arr[26] = v.get('Chorus Off', 0)
    arr[27] = v.get('Chorus I', 0)
    arr[28] = v.get('Chorus II', 0)
    arr[29] = v.get('Octave', 1)
    arr[32] = v.get('LFO Mode', 0)
    arr[33] = v.get('PWM Mode', 0)
    arr[34] = v.get('VCF Env Inv', 0)
    arr[35] = v.get('VCA Mode', 0)
    arr[38] = 1   # Power
    arr[39] = 2   # Porta Mode Poly II
    arr[43] = 0   # ADSR Mode J6
    return arr

def gen_combined_header():
    """Generate combined 256-preset C++ header (128 J6 + 128 J106)."""
    infile = 'tools/juno60_patch_data.csv'
    j106file = 'Source/KR106_Presets_JUCE.h'
    outfile = 'Source/KR106_Presets_JUCE.h'

    # Read J60 patches
    with open(infile) as f:
        j60_rows = list(csv.DictReader(f))

    # Read existing J106 presets
    j106_presets = []
    with open(j106file) as f:
        for line in f:
            if line.strip().startswith('{"'):
                name = line.split('"')[1]
                vals_str = line.split('{', 2)[2].split('}')[0]
                vals = [int(x.strip()) for x in vals_str.split(',')]
                j106_presets.append((name, vals))

    # Build J6 bank (56 patches + 72 init)
    j6_presets = []
    # Map J60 patches into the same A11-A78 slot numbering
    # Slots: group 1-7, patch 1-8 = indices 0-55
    # J60 patches are numbered 11-78, so patch 11 = index 0, 78 = index 55
    slot_map = {}
    for row in j60_rows:
        p = row['Patch'].strip()
        group = int(p[0]) - 1  # 1-7 -> 0-6
        patch = int(p[1]) - 1  # 1-8 -> 0-7
        idx = group * 8 + patch
        slot_map[idx] = row

    for i in range(128):
        if i in slot_map:
            row = slot_map[i]
            # Name with Axx prefix for factory format
            group = i // 8 + 1
            patch = i % 8 + 1
            name = f"A{group}{patch} {row['Name'].strip()}"
            vals = convert_row_flat(row)
        else:
            group = i // 8 + 1
            patch = i % 8 + 1
            prefix = 'A' if i < 64 else 'B'
            gi = (i % 64) // 8 + 1
            pi = i % 8 + 1
            name = f"{prefix}{gi}{pi} Init"
            vals = make_init_preset(adsr_mode=0)
        j6_presets.append((name, vals))

    # Write combined header
    with open(outfile, 'w') as f:
        f.write('#pragma once\n\n')
        f.write('// Auto-generated — 256 factory presets (128 J6 + 128 J106).\n')
        f.write('// Slider values are raw 7-bit (0-127).\n\n')
        f.write('struct KR106FactoryPreset {\n')
        f.write('    const char* name;\n')
        f.write('    int values[44];\n')
        f.write('};\n\n')
        f.write('static constexpr int kNumFactoryPresets = 256;\n\n')
        f.write('static const KR106FactoryPreset kFactoryPresets[] = {\n')
        f.write('    // === J6 Bank (0-127): Juno-60 factory patches ===\n')
        for name, vals in j6_presets:
            f.write(f'    {{"{name}", {{{", ".join(str(v) for v in vals)}}}}},\n')
        f.write('    // === J106 Bank (128-255): Juno-106 factory patches ===\n')
        for name, vals in j106_presets:
            f.write(f'    {{"{name}", {{{", ".join(str(v) for v in vals)}}}}},\n')
        f.write('};\n')

    print(f"Wrote {len(j6_presets) + len(j106_presets)} presets to {outfile}")

if __name__ == '__main__':
    if '--combined' in sys.argv:
        gen_combined_header()
    else:
        main()
