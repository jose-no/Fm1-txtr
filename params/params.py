# SPDX-License-Identifier: GPL-3.0-or-later
"""fm1-txtr — every parameter, defined once.

tools/gen_params.py turns this file into:
  core/gen/tx_params.h, core/gen/tx_params.c   (the firmware's table)
  web/gen/params.js                             (the control page's table)

Each parameter is P(id, name, page, sub, knob, curve, default, ...).

Curves (how the stored value maps to the real one):
  Lin(lo, hi, unit)    stored 0..1000, real = lo + n * (hi - lo)
  Exp(lo, hi, unit)    stored 0..1000, real = lo * (hi / lo) ** n
  Int(lo, hi, unit)    stored as the integer itself
  Enum(labels, values) stored as an index; `values` (optional) gives each label a number

`default` is in real units (or a label for Enum).
Flags: mod=False keeps it out of the mod matrix, morph=False keeps it out of the A/B morph.

Rules for editing this file:
  - Never rename an `id` that has shipped: saved patches find parameters by a hash of the id.
  - New parameters can go anywhere; order only sets the UI order.
"""

import math


class Lin:
    def __init__(self, lo, hi, unit=''):
        self.kind, self.lo, self.hi, self.unit = 'lin', lo, hi, unit


class Exp:
    def __init__(self, lo, hi, unit=''):
        assert lo > 0 and hi > lo
        self.kind, self.lo, self.hi, self.unit = 'exp', lo, hi, unit


class Int:
    def __init__(self, lo, hi, unit=''):
        self.kind, self.lo, self.hi, self.unit = 'int', lo, hi, unit


class Enum:
    def __init__(self, labels, values=None, unit=''):
        self.kind, self.labels, self.values, self.unit = 'enum', list(labels), values, unit
        assert values is None or len(values) == len(labels)


def Pct(default_hi=100):
    return Lin(0, default_hi, '%')


def Bip():
    return Lin(-100, 100, '%')


PARAMS = []


def P(id, name, page, sub, knob, curve, default, mod=None, morph=True, op=None):
    assert len(name) <= 10, f'{id}: name "{name}" is longer than 10 characters (the FM-1 screen)'
    if mod is None:
        mod = curve.kind != 'enum'
    PARAMS.append(dict(id=id, name=name, page=page, sub=sub, knob=knob, curve=curve,
                       default=default, mod=mod, morph=morph, op=op))


# ---------------------------------------------------------------- shared value tables

# Step rate of the sequencer, in bars per step.
RATE_LABELS = ['16 bars', '8 bars', '4 bars', '2 bars', '1 bar', '1/2', '1/4', '1/8']
RATE_BARS = [16, 8, 4, 2, 1, 1 / 2, 1 / 4, 1 / 8]


def free_then_sync(n_free, lo, hi, unit, sync_labels, sync_values, fmt):
    """An Enum whose first n_free entries are free values (log spaced, positive) and whose
    rest are tempo-synced values, stored as negative numbers (-bars) so the engine can tell."""
    labels, values = [], []
    for i in range(n_free):
        v = lo * (hi / lo) ** (i / (n_free - 1))
        labels.append(fmt(v))
        values.append(round(v, 4))
    labels += sync_labels
    values += [-v for v in sync_values]
    return Enum(labels, values, unit)


def fmt_hz(v):
    return f'{v:.2f} Hz' if v < 1 else (f'{v:.1f} Hz' if v < 10 else f'{v:.0f} Hz')


def fmt_ms(v):
    return f'{v:.0f} ms'


LFO_SYNC = ['16 bars', '8 bars', '4 bars', '2 bars', '1 bar', '1/2', '1/4', '1/8', '1/16']
LFO_SYNC_BARS = [16, 8, 4, 2, 1, 1 / 2, 1 / 4, 1 / 8, 1 / 16]
LFO_RATE = free_then_sync(40, 0.01, 20, 'Hz', LFO_SYNC, LFO_SYNC_BARS, fmt_hz)

DLY_SYNC = ['1/32', '1/16', '1/16 d', '1/8 t', '1/8', '1/8 d', '1/4 t', '1/4', '1/4 d', '1/2']
DLY_SYNC_BARS = [1 / 32, 1 / 16, 3 / 32, 1 / 12, 1 / 8, 3 / 16, 1 / 6, 1 / 4, 3 / 8, 1 / 2]
DLY_TIME = free_then_sync(40, 10, 500, 'ms', DLY_SYNC, DLY_SYNC_BARS, fmt_ms)   # the line holds ~0.5 s

SCALES = ['Major', 'Minor', 'Dorian', 'Phrygian', 'Lydian', 'Mixolyd.', 'Locrian', 'Harm min',
          'Mel min', 'Maj pent', 'Min pent', 'Whole', 'Hirajoshi', 'In-sen', 'Chromatic', 'Custom']
NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

MOD_SOURCES = ['Off', 'LFO 1', 'LFO 2', 'LFO 3', 'S&H', 'Walk', 'Follower', 'Note', 'Velocity', 'Drift']

# ---------------------------------------------------------------- SEQ

P('seq_density', 'Density', 'SEQ', 'Rhythm', 0, Int(0, 32, 'hits'), 5)
P('seq_steps', 'Steps', 'SEQ', 'Rhythm', 1, Int(1, 32, 'steps'), 16)
P('seq_rotate', 'Rotate', 'SEQ', 'Rhythm', 2, Int(0, 31), 0)
P('seq_drift', 'Drift', 'SEQ', 'Rhythm', 3, Pct(), 15)

P('seq_bpm', 'Tempo', 'SEQ', 'Clock', 0, Int(40, 240, 'bpm'), 90, mod=False)
P('seq_clock', 'Clock', 'SEQ', 'Clock', 1, Enum(['Internal', 'MIDI']), 'Internal', morph=False)
P('seq_gate', 'Gate', 'SEQ', 'Clock', 2, Lin(5, 100, '%'), 60)
P('seq_vel', 'Velocity', 'SEQ', 'Clock', 3, Int(1, 127), 96)

P('l2_mode', 'Layer 2', 'SEQ', 'Layer 2', 0, Enum(['Off', 'x1 rate', '1/2 rate', '1/4 rate']), 'Off')
P('l2_density', 'L2 Dens', 'SEQ', 'Layer 2', 1, Int(0, 16, 'hits'), 2)
P('l2_steps', 'L2 Steps', 'SEQ', 'Layer 2', 2, Int(1, 16, 'steps'), 8)
P('l2_octave', 'L2 Octave', 'SEQ', 'Layer 2', 3, Int(-3, 0, 'oct'), -2)

P('morph', 'Morph A>B', 'SEQ', 'Perform', 0, Pct(), 0, morph=False)

# ---------------------------------------------------------------- NOTES

P('note_scale', 'Scale', 'NOTES', 'Scale', 0, Enum(SCALES), 'Minor')
P('note_range', 'Range', 'NOTES', 'Scale', 1, Int(1, 4, 'oct'), 2)
P('note_motion', 'Motion', 'NOTES', 'Scale', 2, Enum(['Random', 'Walk', 'Arp up', 'Arp down']), 'Walk')
P('seq_rate', 'Rate', 'NOTES', 'Scale', 3, Enum(RATE_LABELS, RATE_BARS, 'bars'), '1/4')

P('note_root', 'Root', 'NOTES', 'Key', 0, Enum(NOTE_NAMES), 'D')
P('note_octave', 'Octave', 'NOTES', 'Key', 1, Int(1, 6), 3)
P('note_weight', 'Root/5th', 'NOTES', 'Key', 2, Pct(), 30)
P('note_walk', 'Walk step', 'NOTES', 'Key', 3, Int(1, 4, 'deg'), 2)

P('note_custom', 'Custom', 'NOTES', 'Custom', 0, Int(1, 4095), 2741, mod=False)   # bit n = semitone n; 2741 = major

# ---------------------------------------------------------------- SOUND: the FM voice

P('fm_alg', 'Algorithm', 'SOUND', 'Voice', 0, Int(1, 32), 5)
P('fm_fb', 'Feedback', 'SOUND', 'Voice', 1, Int(0, 7), 3)
P('fm_voices', 'Voices', 'SOUND', 'Voice', 2, Int(1, 6), 4, mod=False, morph=False)
P('fm_velsens', 'Vel sens', 'SOUND', 'Voice', 3, Pct(), 50)

P('fm_bright', 'Bright', 'SOUND', 'Tone', 0, Bip(), 0)
P('fm_detune', 'Detune', 'SOUND', 'Tone', 1, Pct(), 12)
P('fm_glide', 'Glide', 'SOUND', 'Tone', 2, Pct(), 0)
P('fm_transpose', 'Transpose', 'SOUND', 'Tone', 3, Int(-24, 24, 'st'), 0)

P('penv_amt', 'Amount', 'SOUND', 'Pitch env', 0, Bip(), 0)
P('penv_att', 'Attack', 'SOUND', 'Pitch env', 1, Pct(), 20)
P('penv_dec', 'Decay', 'SOUND', 'Pitch env', 2, Pct(), 40)
P('penv_vel', 'Vel>Amt', 'SOUND', 'Pitch env', 3, Pct(), 0)

P('amp_a', 'Attack', 'SOUND', 'Amp env', 0, Exp(1, 8000, 'ms'), 300)
P('amp_d', 'Decay', 'SOUND', 'Amp env', 1, Exp(10, 8000, 'ms'), 1200)
P('amp_s', 'Sustain', 'SOUND', 'Amp env', 2, Pct(), 70)
P('amp_r', 'Release', 'SOUND', 'Amp env', 3, Exp(10, 12000, 'ms'), 2500)

# Operators. Defaults: a soft, slightly detuned pad on algorithm 5 (carriers 1, 3, 5).
#            ratio fine level detune  r1  r2  r3  r4   l1  l2  l3  l4  vel krs ams
OP_DEF = {
    1: (1, 0, 96, 0, 55, 40, 30, 45, 99, 92, 85, 0, 2, 1, 0),
    2: (1, 0, 62, 2, 62, 35, 25, 45, 99, 80, 60, 0, 3, 1, 0),
    3: (1, 0, 90, 5, 50, 38, 28, 45, 99, 90, 82, 0, 2, 1, 0),
    4: (3, 0, 48, -2, 66, 30, 22, 45, 99, 70, 50, 0, 3, 2, 0),
    5: (2, 0, 78, -4, 48, 36, 26, 45, 99, 88, 78, 0, 2, 1, 0),
    6: (7, 0, 40, 3, 70, 28, 20, 45, 99, 60, 35, 0, 4, 2, 0),
}
for n in range(1, 7):
    d = OP_DEF[n]
    s = 'Operator'
    P(f'op{n}_ratio', f'Op{n} Ratio', 'SOUND', s, 0, Int(0, 31), d[0], op=n)
    P(f'op{n}_fine', f'Op{n} Fine', 'SOUND', s, 1, Int(0, 99), d[1], op=n)
    P(f'op{n}_level', f'Op{n} Level', 'SOUND', s, 2, Int(0, 99), d[2], op=n)
    P(f'op{n}_detune', f'Op{n} Detun', 'SOUND', s, 3, Int(-7, 7), d[3], op=n)
for n in range(1, 7):
    d = OP_DEF[n]
    for i in range(4):
        P(f'op{n}_r{i + 1}', f'Op{n} Rate{i + 1}', 'SOUND', 'Op rates', i, Int(0, 99), d[4 + i], op=n)
for n in range(1, 7):
    d = OP_DEF[n]
    for i in range(4):
        P(f'op{n}_l{i + 1}', f'Op{n} Levl{i + 1}', 'SOUND', 'Op levels', i, Int(0, 99), d[8 + i], op=n)
for n in range(1, 7):
    d = OP_DEF[n]
    P(f'op{n}_vel', f'Op{n} VelSn', 'SOUND', 'Op extras', 0, Int(0, 7), d[12], op=n)
    P(f'op{n}_krs', f'Op{n} KeyRt', 'SOUND', 'Op extras', 1, Int(0, 7), d[13], op=n)
    P(f'op{n}_ams', f'Op{n} AmpMd', 'SOUND', 'Op extras', 2, Int(0, 3), d[14], op=n)
    P(f'op{n}_on', f'Op{n} On', 'SOUND', 'Op extras', 3, Enum(['Off', 'On']), 'On', op=n)

P('flt_type', 'Type', 'SOUND', 'Filter', 0, Enum(['LP', 'BP', 'HP']), 'LP')
P('flt_cut', 'Cutoff', 'SOUND', 'Filter', 1, Exp(40, 18000, 'Hz'), 5000)
P('flt_res', 'Resonance', 'SOUND', 'Filter', 2, Pct(), 10)
P('flt_env', 'Env amt', 'SOUND', 'Filter', 3, Bip(), 20)

# ---------------------------------------------------------------- GRAIN

P('gr_pos', 'Position', 'GRAIN', 'Core', 0, Pct(), 30)
P('gr_size', 'Size', 'GRAIN', 'Core', 1, Exp(5, 1000, 'ms'), 180)
P('gr_density', 'Density', 'GRAIN', 'Core', 2, Exp(0.5, 120, '/s'), 12)
P('gr_pitch', 'Pitch', 'GRAIN', 'Core', 3, Int(-24, 24, 'st'), 0)

P('gr_spray', 'Pos spray', 'GRAIN', 'Scatter', 0, Pct(), 20)
P('gr_sizernd', 'Size rnd', 'GRAIN', 'Scatter', 1, Pct(), 15)
P('gr_pspray', 'Pitch spr', 'GRAIN', 'Scatter', 2, Pct(), 0)
P('gr_pan', 'Pan spread', 'GRAIN', 'Scatter', 3, Pct(), 60)

P('gr_quant', 'Quantize', 'GRAIN', 'Pitch quant', 0, Enum(['Off', 'On']), 'On')
P('gr_ivset', 'Intervals', 'GRAIN', 'Pitch quant', 1, Enum(['Octave', 'Oct+5th', 'Scale']), 'Oct+5th')
P('gr_ivchance', 'Iv chance', 'GRAIN', 'Pitch quant', 2, Pct(), 15)
P('gr_detrnd', 'Detune rnd', 'GRAIN', 'Pitch quant', 3, Int(0, 50, 'ct'), 6)

P('gr_win', 'Window', 'GRAIN', 'Window', 0, Enum(['Gauss', 'Hann', 'Tri', 'Rect', 'Expodec', 'Rexpodec']), 'Hann')
P('gr_skew', 'Skew', 'GRAIN', 'Window', 1, Bip(), 0)
P('gr_winmorph', 'Win morph', 'GRAIN', 'Window', 2, Pct(), 0)
P('gr_amprnd', 'Amp rnd', 'GRAIN', 'Window', 3, Pct(), 10)

P('gr_rev', 'Reverse', 'GRAIN', 'Direction', 0, Pct(), 10)
P('gr_speed', 'Speed', 'GRAIN', 'Direction', 1, Enum(['1/2', '1', '2'], [0.5, 1, 2]), '1')
P('gr_scan', 'Scan rate', 'GRAIN', 'Direction', 2, Pct(), 0)
P('gr_scandir', 'Scan dir', 'GRAIN', 'Direction', 3, Enum(['Forward', 'Backward', 'Ping-pong']), 'Forward')

P('gr_trig', 'Trigger', 'GRAIN', 'Trigger', 0, Enum(['Free', 'Hits', 'Burst']), 'Free')
P('gr_burst', 'Burst cnt', 'GRAIN', 'Trigger', 1, Int(1, 16), 4)
P('gr_burstspr', 'Burst spr', 'GRAIN', 'Trigger', 2, Exp(1, 2000, 'ms'), 120)
P('gr_jitter', 'Jitter', 'GRAIN', 'Trigger', 3, Pct(), 50)

P('buf_len', 'Length', 'GRAIN', 'Buffer', 0, Exp(0.25, 3.25, 's'), 3)
P('buf_freeze', 'Freeze', 'GRAIN', 'Buffer', 1, Enum(['Off', 'On']), 'Off', morph=False)
P('buf_mode', 'Write', 'GRAIN', 'Buffer', 2, Enum(['Overwrite', 'Overdub']), 'Overwrite')
P('buf_in', 'Input', 'GRAIN', 'Buffer', 3, Pct(), 80)

P('gr_cap', 'Grain cap', 'GRAIN', 'Engine', 0, Int(4, 48, 'grains'), 16, mod=False)
P('gr_mode', 'Mode', 'GRAIN', 'Engine', 1, Enum(['Granular', 'Pitch shift', 'Loop delay']), 'Granular')
P('gr_width', 'Stereo', 'GRAIN', 'Engine', 2, Pct(), 70)
P('gr_level', 'Cloud lvl', 'GRAIN', 'Engine', 3, Pct(), 80)

# ---------------------------------------------------------------- SPACE

P('sp_ftype', 'Type', 'SPACE', 'Filter', 0, Enum(['LP', 'BP', 'HP', 'Notch']), 'LP')
P('sp_fcut', 'Cutoff', 'SPACE', 'Filter', 1, Exp(40, 20000, 'Hz'), 14000)
P('sp_fres', 'Resonance', 'SPACE', 'Filter', 2, Pct(), 5)
P('sp_fdrive', 'Drive', 'SPACE', 'Filter', 3, Pct(), 0)

P('sp_sat', 'Saturate', 'SPACE', 'Color', 0, Pct(), 10)
P('sp_fold', 'Wavefold', 'SPACE', 'Color', 1, Pct(), 0)
P('sp_crush', 'Bitcrush', 'SPACE', 'Color', 2, Pct(), 0)
P('sp_srr', 'Rate red.', 'SPACE', 'Color', 3, Pct(), 0)

P('dl_time', 'Time', 'SPACE', 'Delay', 0, DLY_TIME, '1/8 d')
P('dl_fb', 'Feedback', 'SPACE', 'Delay', 1, Lin(0, 110, '%'), 40)
P('dl_tone', 'Tone', 'SPACE', 'Delay', 2, Pct(), 55)
P('dl_width', 'Ping-pong', 'SPACE', 'Delay', 3, Pct(), 70)

P('dl_mrate', 'Mod rate', 'SPACE', 'Delay mod', 0, Exp(0.05, 8, 'Hz'), 0.3)
P('dl_mdepth', 'Wobble', 'SPACE', 'Delay mod', 1, Pct(), 10)
P('dl_diff', 'Diffusion', 'SPACE', 'Delay mod', 2, Pct(), 25)
P('dl_mix', 'Mix', 'SPACE', 'Delay mod', 3, Pct(), 20)

P('rv_size', 'Size', 'SPACE', 'Reverb', 0, Pct(), 75)
P('rv_decay', 'Decay', 'SPACE', 'Reverb', 1, Exp(0.3, 30, 's'), 7)
P('rv_damp', 'Damping', 'SPACE', 'Reverb', 2, Pct(), 40)
P('rv_pre', 'Pre-delay', 'SPACE', 'Reverb', 3, Lin(0, 100, 'ms'), 20)

P('rv_mod', 'Modulate', 'SPACE', 'Reverb mod', 0, Pct(), 30)
P('rv_shimmer', 'Shimmer', 'SPACE', 'Reverb mod', 1, Pct(), 0)
P('rv_lowcut', 'Low cut', 'SPACE', 'Reverb mod', 2, Exp(20, 1000, 'Hz'), 120)
P('rv_mix', 'Mix', 'SPACE', 'Reverb mod', 3, Pct(), 35)

P('fb_amt', 'Amount', 'SPACE', 'Feedback', 0, Pct(), 0)
P('fb_filter', 'Filter', 'SPACE', 'Feedback', 1, Bip(), 0)
P('fb_pitch', 'Pitch', 'SPACE', 'Feedback', 2, Int(-12, 12, 'st'), 0)
P('fb_sat', 'Saturate', 'SPACE', 'Feedback', 3, Pct(), 20)

P('out_blend', 'FM/Cloud', 'SPACE', 'Output', 0, Pct(), 80)
P('out_width', 'Width', 'SPACE', 'Output', 1, Lin(0, 200, '%'), 100)
P('out_limit', 'Limiter', 'SPACE', 'Output', 2, Pct(), 50)
P('out_level', 'Level', 'SPACE', 'Output', 3, Pct(), 80)

# ---------------------------------------------------------------- MOD

for n, (shape, rate) in enumerate([('Sine', 0.03), ('Tri', 0.11), ('Smooth', '1 bar')], 1):
    s = f'LFO {n}'
    P(f'lfo{n}_shape', 'Shape', 'MOD', s, 0, Enum(['Sine', 'Tri', 'Saw up', 'Saw down', 'Square', 'S&H', 'Smooth']), shape)
    P(f'lfo{n}_rate', 'Rate', 'MOD', s, 1, LFO_RATE, rate)
    P(f'lfo{n}_phase', 'Phase', 'MOD', s, 2, Int(0, 359, 'deg'), 0)
    P(f'lfo{n}_depth', 'Depth', 'MOD', s, 3, Pct(), 100)

P('rnd_sh', 'S&H rate', 'MOD', 'Random', 0, Exp(0.05, 20, 'Hz'), 1)
P('rnd_walk', 'Walk rate', 'MOD', 'Random', 1, Exp(0.01, 5, 'Hz'), 0.2)
P('rnd_smooth', 'Smoothing', 'MOD', 'Random', 2, Pct(), 30)
P('rnd_depth', 'Depth', 'MOD', 'Random', 3, Pct(), 100)

P('ef_sens', 'Sensitiv.', 'MOD', 'Follower', 0, Pct(), 50)
P('ef_att', 'Attack', 'MOD', 'Follower', 1, Exp(1, 500, 'ms'), 10)
P('ef_rel', 'Release', 'MOD', 'Follower', 2, Exp(10, 3000, 'ms'), 300)
P('ef_depth', 'Depth', 'MOD', 'Follower', 3, Pct(), 100)

# Mod matrix. Targets are filled in by the generator (every parameter with mod=True).
MATRIX_DEF = {1: ('LFO 1', 'gr_pos', 20), 2: ('Walk', 'gr_density', 15), 3: ('LFO 2', 'sp_fcut', -10)}
for n in range(1, 9):
    src, dst, amt = MATRIX_DEF.get(n, ('Off', None, 0))
    s = 'Matrix'
    P(f'mx{n}_src', f'M{n} Source', 'MOD', s, 0, Enum(MOD_SOURCES), src)
    P(f'mx{n}_dst', f'M{n} Target', 'MOD', s, 1, Enum(['@targets']), dst or '@none')
    P(f'mx{n}_amt', f'M{n} Amount', 'MOD', s, 2, Int(-100, 100, '%'), amt, mod=False)
    P(f'mx{n}_via', f'M{n} Via', 'MOD', s, 3, Enum(MOD_SOURCES), 'Off')
