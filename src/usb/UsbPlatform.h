// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// UsbPlatform.h - which USB transport this operating system uses (Linux: /dev/hidrawN; Windows: HID API) and the few things that differ for the user.
#pragma once
#include "HidTransport.h"
#include <memory>

namespace vc {

/// A transport for the real calculator on this operating system (Linux: HidrawTransport; Windows: PortTransport over SetupAPI/hid.dll).
std::unique_ptr<HidTransport> makeDefaultTransport();
/// Linux needs a udev rule ("Fix USB Permissions"); Windows needs nothing.
constexpr bool usbNeedsPermissionFix() {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
}

}  // namespace vc
