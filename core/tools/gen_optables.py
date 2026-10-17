#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
"""Generate src/optables.h: what each program byte means, read from the calculator itself.

For every byte value the tool writes that byte as program line 001 into a Continuous Memory
image, loads it, switches to program mode, steps to the line with SST and reads the keycodes the
firmware shows ("001-43,22, A"). Two-byte instructions (15C: first byte xF) are found from how
far SST moves the program pointer, then every second byte is read the same way. The mnemonics
("LBL A") are built from the keycodes and the key legends in app/Sources/Models/Model1*C.swift.
No table from any other source is used.

HP-12C (0.5.6): one byte per line; line 001 is byte 6 of the register at dump 0x7E0, read the
same way (12C mode of the multi firmware, --fw12 E3E3.bin --switch12 1).

usage: gen_optables.py --fw16 B7B7.bin --fw15 E3E3.bin [--switch15 3] [--fw12 E3E3.bin --switch12 1] [--out src/optables.h]
"""
import argparse, os, re, sys, json, time
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import vcpy

# ------------------------------------------------------------------ legends from the Swift models
def legends(model):
    src = open(os.path.join(ROOT, '..', 'app', 'Sources', 'Models', f'Model{model}C.swift'), encoding='utf-8').read()
    keys = {}
    for m in re.finditer(r'KeySpec\((\d+), "([^"]*)"(.*)$', src, re.M):
        code, name, rest = int(m.group(1)), m.group(2), m.group(3)
        def opt(tag):
            mm = re.search(tag + r': "([^"]*)"', rest); return mm.group(1) if mm else None
        keys[code] = {'name': name, 'face': opt('face') or name, 'f': opt('f'), 'g': opt('g')}
    return keys

def keycode(code):          # matrix position -> the calculator's keycode (row, column; column 10 = 0)
    return (code // 10 + 1) * 10 + (code % 10 + 1) % 10

def tidy(raw):
    """'42,21. 0' -> '42,21, 0'; '  45 .0' -> '45 .0' (a point after a digit separates keycodes)."""
    s = re.sub(r'(?<=[0-9A-Za-z])\.', ',', raw)
    return s.strip()

def tokens(codes):
    return [t for t in re.split(r'[ ,]+', codes) if t]

TESTS = {'x≠y', 'x≠0', 'x=y', 'x=0', 'x≤y', 'x<0', 'x>y', 'x>0'}   # shown with '?' as in the handbooks

class Namer:
    def __init__(s, model):
        s.model = model; s.k = legends(model)
        s.by_kc = {keycode(c): v for c, v in s.k.items()}
        s.by_digit = {}
        for c, v in s.k.items():
            if len(v['name']) == 1 and v['name'] in '0123456789ABCDEF': s.by_digit[v['name']] = v
        if model == 16: s.ind = {'31': '(i)', '32': 'I'}
        else: s.ind = {'24': '(i)', '25': 'I'}
        s.labels = {'11': 'A', '12': 'B', '13': 'C', '14': 'D', '15': 'E'} if model == 15 else {}
        if model == 12: s.ind = {}
        s.ops = {'40': '+', '30': '−', '20': '×', '10': '÷'}

    def key(s, t):
        if len(t) == 1: return s.by_digit.get(t.upper())
        if t.isdigit(): return s.by_kc.get(int(t))
        return None

    def arg(s, t, after=None):
        if t.startswith('.'): return t
        if s.model == 12 and t.isdigit(): return t          # registers 0-9, GTO line numbers 00-99
        if len(t) == 1: return t.upper() if s.model == 16 else t
        if t in s.ind: return s.ind[t]
        if t in s.labels: return s.labels[t]
        if after == 'matrix' and t in s.labels: return s.labels[t]
        k = s.key(t)
        if k is None: return t
        if s.model == 15 and k['name'] == 'CHS': return 'MATRIX'
        if s.model == 15 and k['name'] == 'EEX': return 'RESULT'
        if s.model == 15 and k['name'] == 'ENTER': return 'RAN#'
        return k['face']

    def name(s, codes):
        t = tokens(codes)
        if not t: return '', ''
        if len(t) == 1:
            k = s.key(t[0])
            return (k['face'] if k else t[0]), ''
        pfx = {'42': 'f', '43': 'g'}.get(t[0], '')
        if pfx:
            k = s.key(t[1]); fn = (k[pfx] if k and k[pfx] else (k['face'] if k else t[1]))
            if fn in TESTS: fn += '?'
            rest = [s.arg(x) for x in t[2:]]
            return ' '.join([fn] + rest), pfx
        k = s.key(t[0]); fn = k['face'] if k else t[0]
        if s.model == 12 and len(t) == 2 and t[0] in ('44', '45') and t[1] in ('11', '12', '13', '14', '15'):
            return f'{fn} {s.key(t[1])["face"]}', ''               # STO n, RCL PMT (financial registers)
        if s.model == 12 and len(t) == 3 and t[1] == '48':      # STO . 1 -> STO .1
            return f'{fn} .{t[2]}', ''
        if s.model == 12 and len(t) == 3 and t[1] == '43':      # RCL g CFj (the g function as register)
            k2 = s.key(t[2]); return f'{fn} g {(k2["g"] or k2["face"]) if k2 else t[2]}', ''
        if len(t) == 3 and t[1] in s.ops:                       # STO + 1, RCL × .2
            return f'{fn} {s.ops[t[1]]} {s.arg(t[2])}', ''
        if len(t) == 3 and t[1] in ('42', '43'):                # STO g A (15C matrix element)
            return f'{fn} {"f" if t[1] == "42" else "g"} {s.arg(t[2])}', ''
        if len(t) == 3:                                         # RCL DIM (i), STO MATRIX A: the f function
            k2 = s.key(t[1])
            mid = (k2['f'] or k2['face']) if k2 else t[1]
            return f'{fn} {mid} {s.arg(t[2])}', ''
        return ' '.join([fn] + [s.arg(x) for x in t[1:]]), ''

# ------------------------------------------------------------------ reading the firmware
def gen16(fw):
    c = vcpy.Calc(fw); c.keys('HEX g R/S 4 g R/S'); base = bytearray(c.mem())
    top = next(o for o in range(0x100, 0x7F0, 8) if base[o] == 0xF4 and not any(base[o + 1:o + 7]))
    out = {}
    for b in range(256):
        m = bytearray(base); m[top] = b
        x = vcpy.Calc(fw, cold=False); x.load(m); x.keys('g R/S SST')
        t = x.text(); assert t.startswith('001'), (b, t)
        out[b] = tidy(t[4:])
    return out

def gen15(fw, switch):
    c = vcpy.Calc(fw, whole=True)
    if switch: assert c.switch(switch) == 15
    c.keys('BSP 1 9 f SIN COS'); c.ms(1500); c.keys('BSP g R/S')
    for i in range(14): c.keys('4')
    c.keys('g R/S'); c.tap(30); c.ms(500)
    base = bytearray(c.mem()); TOP = 0x7E8; PTR = 0x128
    assert base[TOP] == 0xF4 and base[PTR] == 0xFE, (base[TOP:TOP + 8].hex(), base[PTR:PTR + 2].hex())
    def sample(bs):
        m = bytearray(base)
        for i, b in enumerate(bs): m[TOP + i] = b
        c.load(m); c.tap(30); c.ms(300)
        c.tap(32); c.tap(20)           # g P/R: line 014
        c.tap(10); c.tap(10)           # SST -> 000, SST -> 001
        t1 = c.text()
        c.tap(10); mm = c.mem()        # SST -> 002: the pointer shows the instruction length
        c.tap(32); c.tap(20); c.tap(30); c.ms(300)
        assert t1.startswith('001'), (bs, t1)
        return tidy(t1[4:]), mm[PTR + 1] - 1
    one, two = {}, {}
    for b in range(256):
        one[b] = sample([b, 0xF5, 0xF6]); print(f'15C {b:02X} {one[b]}', file=sys.stderr, flush=True)
    for p in [b for b in range(256) if one[b][1] == 2]:
        two[p] = {b: sample([p, b, 0xF6])[0] for b in range(256)}
        print(f'15C {p:02X}xx done', file=sys.stderr, flush=True)
    return one, two

def gen12(fw, switch):
    c = vcpy.Calc(fw, whole=True)
    if switch: assert c.switch(switch) == 12
    def pm(): c.tap(31); c.tap(20); c.ms(300)                # f P/R
    def snap():
        c.tap(30); c.ms(600); m = bytearray(c.mem()); c.tap(30); c.ms(1000); return m
    pm(); c.tap(26); pm(); base = snap()                      # a one-line program: line 01 at 0x7E6
    assert base[0x7E6] == 0x1F, base[0x7E0:0x7E8].hex()
    out = {}
    for b in range(256):
        m = bytearray(base); m[0x7E6] = b
        c.tap(30); c.ms(400); c.load(m); c.tap(30); c.ms(800)
        pm(); c.tap(21); t = c.text(); pm()                    # SST -> 01
        assert t.startswith('01-'), (b, t)
        out[b] = tidy(t[3:])
    return out

# ------------------------------------------------------------------ output

def cs(s):
    out = ''
    for ch in s:
        if 32 <= ord(ch) < 127 and ch not in '"\\': out += ch
        else: out += ''.join(f'\\x{b:02x}' for b in ch.encode()) + '" "'
    return '"' + out + '"'

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fw16'); ap.add_argument('--fw15'); ap.add_argument('--switch15', type=int, default=0)
    ap.add_argument('--fw12'); ap.add_argument('--switch12', type=int, default=1)
    ap.add_argument('--cache', default='/tmp/optables.json'); ap.add_argument('--out', default=os.path.join(ROOT, 'src', 'optables.h'))
    a = ap.parse_args()
    data = json.load(open(a.cache)) if os.path.exists(a.cache) else {}
    if '16' not in data:
        t = time.time(); data['16'] = gen16(a.fw16); print(f'16C: {time.time() - t:.0f} s', file=sys.stderr)
        json.dump(data, open(a.cache, 'w'))
    if '15' not in data:
        t = time.time(); one, two = gen15(a.fw15, a.switch15); data['15'] = {'one': one, 'two': two}
        print(f'15C: {time.time() - t:.0f} s', file=sys.stderr); json.dump(data, open(a.cache, 'w'))
    if '12' not in data:
        t = time.time(); data['12'] = gen12(a.fw12, a.switch12); print(f'12C: {time.time() - t:.0f} s', file=sys.stderr)
        json.dump(data, open(a.cache, 'w'))
    d16 = {int(k): v for k, v in data['16'].items()}
    d12 = {int(k): v for k, v in data['12'].items()}
    one = {int(k): v for k, v in data['15']['one'].items()}
    two = {int(p): {int(b): v for b, v in t.items()} for p, t in data['15']['two'].items()}
    n16, n15, n12 = Namer(16), Namer(15), Namer(12)
    L = ['/* optables.h - GENERATED by tools/gen_optables.py, do not edit.',
         ' *',
         ' * Meaning of each user-program byte, as the calculator\'s own firmware shows it in program',
         ' * mode (keycodes), with a mnemonic built from the key legends of the app\'s model data.',
         ' * HP-16C and HP-12C: one byte per line. HP-15C: one byte, or two bytes when the first one is xF.',
         ' *',
         ' * SPDX-License-Identifier: MIT',
         ' * Copyright (c) 2026 José González Divassón */',
         '#ifndef VC_OPTABLES_H', '#define VC_OPTABLES_H',
         'typedef struct { const char *keys, *mn; char pfx; } vc_op;', '']
    def emit(name, tab, namer):
        L.append(f'static const vc_op {name}[256] = {{')
        for b in range(256):
            mn, pfx = namer.name(tab[b])
            L.append(f'    {{{cs(tab[b])}, {cs(mn)}, {repr(pfx) if pfx else 0}}}, /* {b:02X} */')
        L.append('};')
    emit('OP16', d16, n16)
    emit('OP12', d12, n12)
    emit('OP15', {b: v[0] for b, v in one.items()}, n15)
    L.append('static const unsigned char OP15_LEN[256] = {' + ','.join(str(one[b][1]) for b in range(256)) + '};')
    groups = []; gidx = {}
    for p in sorted(two):
        key = tuple(two[p][b] for b in range(256))
        if key not in gidx: gidx[key] = len(groups); groups.append(key)
        two[p]['g'] = gidx[key]
    for gi, g in enumerate(groups): emit(f'OP15X_{gi}', dict(enumerate(g)), n15)
    L.append('static const vc_op *const OP15X[256] = {' + ', '.join(f'OP15X_{two[p]["g"]}' if p in two else 'NULL' for p in range(256)) + '};')
    L.append('#endif')
    open(a.out, 'w', encoding='utf-8').write('\n'.join(L) + '\n')
    print(f'wrote {a.out}: 16C 256, 15C 256 + {len(two)} two-byte prefixes in {len(groups)} groups', file=sys.stderr)
main()
