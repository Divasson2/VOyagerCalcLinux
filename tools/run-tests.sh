#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
# Configure, build and run the Linux port's tests. Usage: tools/run-tests.sh [/path/to/Qt/6.x/gcc_64]
# Set VOYAGERCALC_TEST_FIRMWARE to a firmware file you own to include the boot-and-type test.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
QTARG=""; [ -n "$1" ] && QTARG="-DCMAKE_PREFIX_PATH=$1"
cmake -S "$HERE" -B "$HERE/build" -G Ninja $QTARG
cmake --build "$HERE/build"
cd "$HERE/build" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
