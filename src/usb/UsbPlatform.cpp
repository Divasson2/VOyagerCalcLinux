// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "UsbPlatform.h"
#ifdef _WIN32
#include "HidPort.h"
#include "PortTransport.h"
#else
#include "HidrawTransport.h"
#endif

namespace vc {

std::unique_ptr<HidTransport> makeDefaultTransport() {
#ifdef _WIN32
    return std::unique_ptr<HidTransport>(new PortTransport(platformHidBackend()));
#else
    return std::unique_ptr<HidTransport>(new HidrawTransport());
#endif
}

}  // namespace vc
