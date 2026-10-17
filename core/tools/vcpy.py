# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
"""Small ctypes binding of the native core, for tools and tests written in Python.
Builds build/libvoyagercore.so from src/ on first use (needs clang or cc)."""
import ctypes as C, os, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
SO = os.path.join(ROOT, 'build', 'libvoyagercore.so')
def _build():
    src = [os.path.join(ROOT, 'src', f) for f in sorted(os.listdir(os.path.join(ROOT, 'src'))) if f.endswith('.c')]
    if os.path.exists(SO) and all(os.path.getmtime(SO) >= os.path.getmtime(f) for f in src + [os.path.join(ROOT, 'include', 'voyager_core.h')]):
        return
    os.makedirs(os.path.dirname(SO), exist_ok=True)
    cc = os.environ.get('CC', 'clang')
    subprocess.check_call([cc, '-O2', '-shared', '-fPIC', '-I' + os.path.join(ROOT, 'include')] + src + ['-o', SO])
_build()
L = C.CDLL(SO)
L.vc_create.restype = C.c_void_p
L.vc_key.argtypes = [C.c_void_p, C.c_int, C.c_uint64]
L.vc_key_code.argtypes = [C.c_int, C.c_char_p]
L.vc_debug_read32.restype = C.c_uint32; L.vc_debug_read32.argtypes = [C.c_void_p, C.c_uint32]
for f in ('vc_load_firmware', 'vc_start', 'vc_model', 'vc_model_variant', 'vc_save_memory', 'vc_load_memory', 'vc_run_ms',
          'vc_press_key', 'vc_release_key', 'vc_get_display_cells', 'vc_display_text', 'vc_destroy'):
    getattr(L, f).argtypes = None

class Disp(C.Structure):
    _fields_ = [('seg', C.c_uint8 * 10), ('dot', C.c_uint8 * 10), ('comma', C.c_uint8 * 10), ('minus', C.c_uint8),
                ('res', C.c_uint8 * 3), ('ann', C.c_uint32), ('annbits', C.c_uint64)]

class Calc:
    """whole=False: program runner (16C CE firmware only); whole=True: whole calculator, switched on."""
    def __init__(s, fw, whole=False, cold=True):
        s.c = C.c_void_p(L.vc_create())
        d = open(fw, 'rb').read()
        if L.vc_load_firmware(s.c, d, C.c_size_t(len(d))): raise RuntimeError('firmware not recognised: ' + fw)
        s.whole = whole
        if L.vc_start(s.c, 1 if whole else 0, 1 if cold else 0): raise RuntimeError('start failed')
        if whole: s.tap(30); s.ms(1500)
    def __del__(s):
        try: L.vc_destroy(s.c)
        except Exception: pass
    def model(s): return L.vc_model(s.c)
    def ms(s, n): L.vc_run_ms(s.c, C.c_uint32(n))
    def code(s, k):
        r = L.vc_key_code(s.model() or 16, k.encode())
        if r < 0: raise KeyError(k)
        return r
    def tap(s, code, hold=120, after=600):
        if not s.whole: return L.vc_key(s.c, code, 0)
        L.vc_press_key(s.c, code); s.ms(hold); L.vc_release_key(s.c, code); s.ms(after)
    def keys(s, seq):
        """Key names separated by spaces; a word that is no key name is typed digit by digit."""
        for k in seq.split():
            if len(k) > 1 and L.vc_key_code(s.model() or 16, k.encode()) < 0:
                for ch in k: s.tap(s.code(ch))
            else: s.tap(s.code(k))
    def mem(s):
        b = (C.c_uint8 * 2048)()
        if L.vc_save_memory(s.c, b): raise RuntimeError('save')
        return bytes(b)
    def load(s, data): return L.vc_load_memory(s.c, bytes(data))
    def text(s):
        d = Disp(); L.vc_get_display_cells(s.c, C.byref(d))
        b = C.create_string_buffer(64); L.vc_display_text(C.byref(d), b, C.c_size_t(64)); return b.value.decode()
    def rd32(s, a): return L.vc_debug_read32(s.c, a)
    def switch(s, pick):
        """HP-12C CE multi: off, g-ENTER-ON, 4, pick (1 = 12C, 3 = 15.2, 5 = 16C), clear Pr Error."""
        s.tap(30); s.ms(500)
        if s.text().strip(): s.tap(30); s.ms(500)
        L.vc_press_key(s.c, 32); L.vc_press_key(s.c, 25); s.ms(100); L.vc_press_key(s.c, 30); s.ms(300)
        L.vc_release_key(s.c, 30); s.ms(200); L.vc_release_key(s.c, 25); L.vc_release_key(s.c, 32); s.ms(800)
        s.tap(16); s.tap(pick); s.ms(2000); s.tap(24)
        return s.model()

# ------------------------------------------------------------------ inspection API
class Value(C.Structure):
    _fields_ = [('kind', C.c_int), ('raw', C.c_uint64), ('word', C.c_uint64), ('negative', C.c_int), ('number', C.c_double),
                ('text', C.c_char * 72), ('dec', C.c_char * 32), ('name', C.c_char * 12)]
class Info(C.Structure):
    _fields_ = [(n, C.c_int) for n in ('model', 'variant', 'base', 'word_size', 'complement')] + [('flags', C.c_uint32)] + \
               [(n, C.c_int) for n in ('prgm_mode', 'running', 'line', 'lines', 'registers', 'has_i', 'has_program', 'ext_memory')] + \
               [('reserved', C.c_int * 6)]
class ProgLine(C.Structure):
    _fields_ = [('line', C.c_int), ('code', C.c_uint8 * 2), ('len', C.c_uint8), ('prefix', C.c_char),
                ('keys', C.c_char * 16), ('mnemonic', C.c_char * 28)]
for f in ('vc_inspect', 'vc_inspect_stack', 'vc_inspect_register', 'vc_inspect_program'): getattr(L, f).argtypes = None
def _v(v): return {'name': v.name.decode(), 'text': v.text.decode(), 'dec': v.dec.decode(), 'kind': v.kind, 'word': v.word, 'raw': v.raw, 'neg': v.negative, 'number': v.number}
def info(calc):
    i = Info(); r = L.vc_inspect(calc.c, C.byref(i)); assert r == 0, r
    return {n: getattr(i, n) for n, _ in Info._fields_ if n != 'reserved'}
def stack(calc, reg):
    v = Value(); r = L.vc_inspect_stack(calc.c, reg, C.byref(v)); return _v(v) if r == 0 else None
def register(calc, n):
    v = Value(); r = L.vc_inspect_register(calc.c, n, C.byref(v)); return _v(v) if r == 0 else None
def program(calc, first=1, n=1000):
    buf = (ProgLine * n)(); k = L.vc_inspect_program(calc.c, first, buf, n)
    return [(p.line, p.keys.decode(), p.mnemonic.decode('utf-8'), p.prefix.decode() if p.prefix else '') for p in buf[:k]]
