#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
#
# Builds the Windows test packages from a cross-compiled build (llvm-mingw + Qt for Windows):
#   VoyagerCalc-<version>-windows-<arch>[-<tag>].zip          portable folder: unzip anywhere, run VoyagerCalc.exe (no installation)
#   VoyagerCalc-<version>-windows-<arch>[-<tag>]-setup.exe    per-user installer (NSIS, no administrator rights): Start menu + uninstall entry
# It bundles voyagercalc.exe, the console variant, the Qt DLLs it needs (found by reading the PE import tables, nothing is guessed), the Qt platform and
# style plugins, the llvm-mingw runtime DLLs, and the texts. NEVER adds firmware: the output is scanned and the script stops if it finds a firmware-sized .bin.
#
# usage: build-package.py --arch x64|arm64 --build DIR --qt QTDIR --mingw-bin DIR --objdump PATH --out DIR [--tag test1] [--makensis PATH --nsisdir DIR]
import argparse, os, re, shutil, subprocess, sys, zipfile, hashlib

ap = argparse.ArgumentParser()
ap.add_argument('--arch', required=True, choices=['x64', 'arm64'])
ap.add_argument('--build', required=True)
ap.add_argument('--qt', required=True)
ap.add_argument('--mingw-bin', required=True)
ap.add_argument('--objdump', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('--tag', default='')
ap.add_argument('--version', default='0.7.0')
ap.add_argument('--makensis', default='')
ap.add_argument('--nsisdir', default='')
a = ap.parse_args()

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
name = f'VoyagerCalc-{a.version}-windows-{a.arch}' + (f'-{a.tag}' if a.tag else '')
stage = os.path.join(a.out, 'stage-win-' + a.arch)
root = os.path.join(stage, 'VoyagerCalc')
shutil.rmtree(stage, ignore_errors=True)
os.makedirs(root)

def put(src, dst):
    os.makedirs(os.path.dirname(os.path.join(root, dst)) or root, exist_ok=True)
    shutil.copy2(src, os.path.join(root, dst))

put(os.path.join(a.build, 'voyagercalc.exe'), 'VoyagerCalc.exe')
put(os.path.join(a.build, 'voyagercalc-cli.exe'), 'VoyagerCalc-cli.exe')
strip = os.path.join(os.path.dirname(a.objdump), 'llvm-strip')
if os.path.exists(strip): subprocess.check_call([strip, '--strip-unneeded', os.path.join(root, 'VoyagerCalc.exe'), os.path.join(root, 'VoyagerCalc-cli.exe')])

search = [os.path.join(a.qt, 'bin'), a.mingw_bin]
system = re.compile(r'^(api-ms-win-.*|kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|ws2_32|winmm|imm32|comdlg32|comctl32|version|uxtheme|dwmapi|setupapi|hid|crypt32|bcrypt|ntdll|msvcrt|ucrtbase|shlwapi|d3d11|dxgi|dwrite|d2d1|d3d9|opengl32|netapi32|userenv|wtsapi32|winspool\.drv|mpr|secur32|iphlpapi|dnsapi|rpcrt4|powrprof|propsys|cfgmgr32|dbghelp|psapi|normaliz|d3d12|shcore|wininet|dcomp|gdiplus|cabinet|authz|comdlg32)(\.dll)?$', re.I)

def imports(path):
    out = subprocess.check_output([a.objdump, '-p', path], text=True, errors='replace')
    return re.findall(r'DLL Name: (\S+)', out)

need, queue = {}, [os.path.join(root, 'VoyagerCalc.exe'), os.path.join(root, 'VoyagerCalc-cli.exe')]
plugins = [('platforms', 'qwindows.dll'), ('styles', 'qmodernwindowsstyle.dll')]
for sub, f in plugins:
    src = os.path.join(a.qt, 'plugins', sub, f)
    if os.path.exists(src): put(src, os.path.join(sub, f)); queue.append(src)
while queue:
    p = queue.pop()
    for d in imports(p):
        if d.lower() in need or system.match(d): continue
        found = next((os.path.join(s, d) for s in search if os.path.exists(os.path.join(s, d))), None)
        if not found:
            found = next((os.path.join(s, x) for s in search for x in (os.listdir(s) if os.path.isdir(s) else []) if x.lower() == d.lower()), None)
        if not found: print('WARNING: import not found (assumed to be a Windows system DLL):', d); need[d.lower()] = None; continue
        need[d.lower()] = found; put(found, os.path.basename(found)); queue.append(found)
print('bundled DLLs:', ' '.join(sorted(os.path.basename(v) for v in need.values() if v)))

for src, dst in [('LICENSE', 'LICENSE.txt'), ('packaging/windows/README-windows.txt', 'README.txt'), ('packaging/windows/INSTALL-windows.md', 'INSTALL.md')]:
    put(os.path.join(ROOT, src), dst)
shutil.copy2(os.path.join(HERE, 'VoyagerCalc USB Test.bat'), os.path.join(root, 'VoyagerCalc USB Test.bat'))
# the tests' qt.conf: nothing needed, plugins sit in platforms\ and styles\ next to the exe

# nothing that looks like firmware may be in the package (an HP ROM image is a few hundred KB .bin)
for dp, _, fs in os.walk(root):
    for f in fs:
        fp = os.path.join(dp, f)
        if f.lower().endswith('.bin') or (f.lower().endswith(('.rom', '.img')) and os.path.getsize(fp) > 100000): sys.exit('STOP: firmware-like file in package: ' + fp)

os.makedirs(a.out, exist_ok=True)
zpath = os.path.join(a.out, name + '.zip')
with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for dp, _, fs in os.walk(root):
        for f in sorted(fs):
            fp = os.path.join(dp, f); z.write(fp, os.path.relpath(fp, stage))
def md5(p): return hashlib.md5(open(p, 'rb').read()).hexdigest()
open(zpath + '.md5', 'w').write(f'{md5(zpath)}  {os.path.basename(zpath)}\n')
print(zpath, os.path.getsize(zpath), 'bytes md5', md5(zpath))

if a.makensis:
    exe = os.path.join(a.out, name + '-setup.exe')
    env = dict(os.environ, NSISDIR=a.nsisdir) if a.nsisdir else os.environ
    subprocess.check_call([a.makensis, '-V2', f'-DVERSION={a.version}', f'-DSRCDIR={root}', f'-DOUTFILE={exe}', f'-DARCH={a.arch}', f'-DICON={os.path.join(HERE, "voyagercalc.ico")}', os.path.join(HERE, 'installer.nsi')], env=env)
    open(exe + '.md5', 'w').write(f'{md5(exe)}  {os.path.basename(exe)}\n')
    print(exe, os.path.getsize(exe), 'bytes md5', md5(exe))
