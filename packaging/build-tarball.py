#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
#
# Builds the Linux test tarball:  VoyagerCalc-<version>-linux-<arch>[-<tag>].tar.gz
# It bundles the program (built with -DVC_PORTABLE_RPATH=ON), the Qt libraries it needs (found by reading the ELF
# NEEDED entries, nothing is guessed), the Qt plugins, install.sh / uninstall.sh and the texts.
# NEVER adds firmware: the finished tarball is scanned and the script stops if it finds a firmware-sized .bin.
#
# usage: build-tarball.py --arch arm64|x86_64 --build DIR --qt QTDIR --extra DIR [--extra DIR ...] --readelf PATH --strip PATH
#                         --out DIR [--tag test1]
import argparse, hashlib, os, re, shutil, subprocess, sys, tarfile, io

ap = argparse.ArgumentParser()
ap.add_argument('--arch', required=True)
ap.add_argument('--build', required=True)
ap.add_argument('--qt', required=True)
ap.add_argument('--extra', action='append', default=[])
ap.add_argument('--readelf', default='readelf')
ap.add_argument('--strip', default='strip')
ap.add_argument('--out', required=True)
ap.add_argument('--tag', default='')
ap.add_argument('--version', default='0.7.1')
a = ap.parse_args()

HERE = os.path.dirname(os.path.abspath(__file__))
LINUX = os.path.dirname(HERE)
name = f'VoyagerCalc-{a.version}-linux-{a.arch}' + (f'-{a.tag}' if a.tag else '')
stage = os.path.join(a.out, 'stage-' + a.arch)
root = os.path.join(stage, name)
app = os.path.join(root, 'VoyagerCalc')
if os.path.exists(root):
    shutil.move(root, root + '.previous-%d' % os.getpid())
for d in ('bin', 'lib', 'plugins', 'share'):
    os.makedirs(os.path.join(app, d))

BUNDLE = re.compile(r'^(libQt6.*|libicu.*|libxcb-(cursor|icccm|image|keysyms|randr|render|render-util|shape|shm|sync|util|xfixes|xinerama|xinput|xkb)\.so.*|libxkbcommon-x11\.so.*|libOpenGL\.so.*|libSM\.so.*|libICE\.so.*|libX11-xcb\.so.*)$')
libdirs = [os.path.join(a.qt, 'lib')]
for e in a.extra:
    libdirs += [os.path.join(e, 'usr/lib/' + t) for t in ('x86_64-linux-gnu', 'aarch64-linux-gnu')] + [e]

def needed(path):
    out = subprocess.check_output([a.readelf, '-d', path], text=True)
    return re.findall(r'\(NEEDED\)\s+Shared library: \[(.+?)\]', out)

def find(n):
    for d in libdirs:
        p = os.path.join(d, n)
        if os.path.exists(p):
            return os.path.realpath(p)
    return None

bundled, system = {}, set()
def add_deps(path):
    for n in needed(path):
        if n in bundled or n in system:
            continue
        if BUNDLE.match(n):
            p = find(n)
            if not p:
                print('MISSING (should be bundled):', n, 'needed by', os.path.basename(path)); system.add(n); continue
            dst = os.path.join(app, 'lib', n)
            shutil.copy2(p, dst)
            bundled[n] = dst
            add_deps(p)
        else:
            system.add(n)

# program
exe = os.path.join(app, 'bin', 'voyagercalc')
shutil.copy2(os.path.join(a.build, 'voyagercalc'), exe)
subprocess.check_call([a.strip, exe])
add_deps(exe)

# plugins (only those that are useful for a desktop calculator)
PLUGINS = ['platforms/libqxcb.so', 'platforms/libqwayland-generic.so', 'platforms/libqoffscreen.so',
           'wayland-shell-integration/libxdg-shell.so', 'wayland-decoration-client/libbradient.so',
           'platformthemes/libqxdgdesktopportal.so', 'platformthemes/libqgtk3.so',
           'platforminputcontexts/libcomposeplatforminputcontextplugin.so', 'platforminputcontexts/libibusplatforminputcontextplugin.so',
           'imageformats/libqjpeg.so', 'imageformats/libqgif.so', 'imageformats/libqico.so']
for pl in PLUGINS:
    src = os.path.join(a.qt, 'plugins', pl)
    if not os.path.exists(src):
        print('plugin not in this Qt:', pl); continue
    dst = os.path.join(app, 'plugins', pl)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    add_deps(src)

with open(os.path.join(app, 'bin', 'qt.conf'), 'w') as f:
    f.write('[Paths]\nPrefix = ..\nPlugins = plugins\nLibraries = lib\n')

sh = os.path.join(app, 'share')
shutil.copy2(os.path.join(LINUX, 'assets', 'icon', 'AppIcon-256.png'), os.path.join(sh, 'voyagercalc.png'))
open(os.path.join(sh, 'ARCH'), 'w').write(a.arch + '\n')
open(os.path.join(sh, 'VERSION'), 'w').write(a.version + '\n')
env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
rule = subprocess.check_output([os.environ.get('VC_RUN', ''), exe, '--print-udev-rule'] if os.environ.get('VC_RUN') else [exe, '--print-udev-rule'], env=dict(env, LD_LIBRARY_PATH=os.path.join(app, 'lib') + ':' + os.environ.get('VC_EXTRA_LD', '')))
open(os.path.join(sh, '99-voyagercalc.rules'), 'wb').write(rule)

shutil.copy2(os.path.join(HERE, 'usb-access.sh'), os.path.join(sh, 'usb-access.sh')); os.chmod(os.path.join(sh, 'usb-access.sh'), 0o755)
for f in ('install.sh', 'uninstall.sh'):
    shutil.copy2(os.path.join(HERE, f), os.path.join(root, f))
for f, dst in (('README-linux.txt', 'README.txt'), ('THIRD-PARTY.txt', 'THIRD-PARTY.txt'), ('LICENSE.txt', 'LICENSE.txt')):
    shutil.copy2(os.path.join(HERE, f), os.path.join(root, dst))
os.chmod(os.path.join(root, 'install.sh'), 0o755); os.chmod(os.path.join(root, 'uninstall.sh'), 0o755)

# --- safety: no firmware, ever -------------------------------------------------------------------------------------
for dp, dn, fn in os.walk(root):
    for f in fn:
        p = os.path.join(dp, f)
        if f.lower().endswith('.bin') or (os.path.getsize(p) in range(100000, 130000) and f.lower().endswith(('.fw', '.rom', '.img'))):
            sys.exit('REFUSING: firmware-like file in the package: ' + p)

tar = os.path.join(a.out, name + '.tar.gz')
with tarfile.open(tar, 'w:gz', compresslevel=9) as t:
    t.add(root, arcname=name, filter=lambda ti: (setattr(ti, 'uid', 0), setattr(ti, 'gid', 0), setattr(ti, 'uname', ''), setattr(ti, 'gname', ''), ti)[-1])
md5 = hashlib.md5(open(tar, 'rb').read()).hexdigest()
open(tar + '.md5', 'w').write(f'{md5}  {os.path.basename(tar)}\n')
print('bundled libs:', ' '.join(sorted(bundled)))
print('system libs expected (all standard on a Linux desktop):', ' '.join(sorted(system)))
print(tar, os.path.getsize(tar), 'bytes md5', md5)
