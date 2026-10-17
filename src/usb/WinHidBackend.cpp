// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// WinHidBackend.cpp - Windows: HID devices through SetupAPI (list) and hid.dll (VID/PID, name, report lengths), I/O with overlapped ReadFile / WriteFile.
// No driver is installed: the calculator in USB mode (f + ENTER + ON) is a plain HID device that Windows' own HID class driver already handles.
#ifdef _WIN32
#include "HidPort.h"
#include "UsbLog.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <vector>

namespace vc {
namespace {

QString winError(DWORD e) {
    wchar_t *msg = nullptr;
    DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, reinterpret_cast<wchar_t *>(&msg), 0, nullptr);
    QString s = n ? QString::fromWCharArray(msg, int(n)).trimmed() : QString();
    if (msg) ::LocalFree(msg);
    return QStringLiteral("%1 (Windows error %2)").arg(s.isEmpty() ? QStringLiteral("unknown error") : s).arg(e);
}

LinkError openError(DWORD e) {
    if (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION) return LinkError::accessDenied(QString(), QStringLiteral("Windows says another program is using the calculator (%1). Close VoyagerInterface, the HP software or another copy of VoyagerCalc, unplug the calculator, plug it in again and press f ENTER ON. ").arg(winError(e)));
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_DEVICE_NOT_CONNECTED || e == ERROR_NO_SUCH_DEVICE) return LinkError(LinkError::NoDevice, QStringLiteral("The calculator went away (%1). ").arg(winError(e)) + LinkError::connectHint());
    return LinkError::io(QStringLiteral("opening the calculator failed: %1").arg(winError(e)));
}

class Win32Port : public HidPort {
public:
    Win32Port(HANDLE h, int inLen) : h_(h), inLen_(inLen), buf_(size_t(std::max(inLen, 65)), 0) {
        rd_.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wr_.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    ~Win32Port() override { close(); if (rd_.hEvent) ::CloseHandle(rd_.hEvent); if (wr_.hEvent) ::CloseHandle(wr_.hEvent); }

    void write(const uint8_t *data, size_t n, int timeoutMs) override {
        if (h_ == INVALID_HANDLE_VALUE) throw LinkError::noDevice();
        ::ResetEvent(wr_.hEvent);
        DWORD done = 0;
        if (!::WriteFile(h_, data, DWORD(n), &done, &wr_)) {
            DWORD e = ::GetLastError();
            if (e != ERROR_IO_PENDING) throw LinkError::io(QStringLiteral("WriteFile failed: %1").arg(winError(e)));
            if (::WaitForSingleObject(wr_.hEvent, DWORD(timeoutMs)) != WAIT_OBJECT_0) {
                ::CancelIoEx(h_, &wr_);
                ::GetOverlappedResult(h_, &wr_, &done, TRUE);          // reap it: the OVERLAPPED must outlive the request
                throw LinkError::stuck(QStringLiteral("send the request to the calculator"), timeoutMs);
            }
            if (!::GetOverlappedResult(h_, &wr_, &done, FALSE)) throw LinkError::io(QStringLiteral("WriteFile failed: %1").arg(winError(::GetLastError())));
        }
        if (done != n) throw LinkError::io(QStringLiteral("only %1 of %2 bytes were written").arg(done).arg(n));
    }

    size_t read(uint8_t *out, size_t cap, int timeoutMs) override {
        if (h_ == INVALID_HANDLE_VALUE) throw LinkError::noDevice();
        // One read stays pending between calls (a poll that times out does not cancel it, so no report can be lost between two polls).
        if (!pending_) {
            ::ResetEvent(rd_.hEvent);
            DWORD got = 0;
            if (::ReadFile(h_, buf_.data(), DWORD(inLen_), &got, &rd_)) return deliver(out, cap, got);
            DWORD e = ::GetLastError();
            if (e != ERROR_IO_PENDING) throw gone(e);
            pending_ = true;
        }
        const DWORD w = ::WaitForSingleObject(rd_.hEvent, DWORD(timeoutMs));
        if (w == WAIT_TIMEOUT) return 0;
        DWORD got = 0;
        pending_ = false;
        if (!::GetOverlappedResult(h_, &rd_, &got, FALSE)) throw gone(::GetLastError());
        return deliver(out, cap, got);
    }

    void close() override {
        if (h_ == INVALID_HANDLE_VALUE) return;
        if (pending_) { ::CancelIoEx(h_, &rd_); DWORD n; ::GetOverlappedResult(h_, &rd_, &n, TRUE); pending_ = false; }
        ::CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }

private:
    static LinkError gone(DWORD e) { return LinkError(LinkError::NoDevice, QStringLiteral("The calculator went away (%1). ").arg(winError(e)) + LinkError::connectHint()); }
    size_t deliver(uint8_t *out, size_t cap, DWORD got) { const size_t n = std::min<size_t>(got, cap); std::copy(buf_.begin(), buf_.begin() + n, out); return n; }
    HANDLE h_;
    int inLen_;
    std::vector<uint8_t> buf_;
    OVERLAPPED rd_{}, wr_{};
    bool pending_ = false;
};

class Win32Backend : public HidBackend {
public:
    std::vector<HidDeviceInfo> enumerate() override {
        std::vector<HidDeviceInfo> out;
        GUID guid; ::HidD_GetHidGuid(&guid);
        HDEVINFO set = ::SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) throw LinkError::io(QStringLiteral("SetupDiGetClassDevs failed: %1").arg(winError(::GetLastError())));
        for (DWORD i = 0;; ++i) {
            SP_DEVICE_INTERFACE_DATA did{}; did.cbSize = sizeof did;
            if (!::SetupDiEnumDeviceInterfaces(set, nullptr, &guid, i, &did)) break;
            DWORD need = 0;
            ::SetupDiGetDeviceInterfaceDetailW(set, &did, nullptr, 0, &need, nullptr);
            if (need == 0) continue;
            std::vector<uint8_t> mem(need);
            auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(mem.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!::SetupDiGetDeviceInterfaceDetailW(set, &did, detail, need, nullptr, nullptr)) continue;
            HidDeviceInfo d; d.path = QString::fromWCharArray(detail->DevicePath);
            // access 0: ask questions without taking the device (mice and keyboards are opened exclusively by Windows)
            HANDLE h = ::CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) { UsbLog::write(QStringLiteral("discovery: %1: cannot query (%2), skipped").arg(d.path, winError(::GetLastError()))); continue; }
            HIDD_ATTRIBUTES a{}; a.Size = sizeof a;
            if (::HidD_GetAttributes(h, &a)) { d.vendor = a.VendorID; d.product = a.ProductID; }
            wchar_t name[128] = {0};
            if (::HidD_GetProductString(h, name, sizeof name)) d.name = QString::fromWCharArray(name);
            PHIDP_PREPARSED_DATA pp = nullptr;
            if (::HidD_GetPreparsedData(h, &pp)) {
                HIDP_CAPS caps{};
                if (::HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) { d.inputReportBytes = caps.InputReportByteLength; d.outputReportBytes = caps.OutputReportByteLength; }
                ::HidD_FreePreparsedData(pp);
            }
            ::CloseHandle(h);
            out.push_back(d);
        }
        ::SetupDiDestroyDeviceInfoList(set);
        return out;
    }

    std::unique_ptr<HidPort> open(const HidDeviceInfo &d) override {
        HANDLE h = ::CreateFileW(reinterpret_cast<const wchar_t *>(d.path.utf16()), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) throw openError(::GetLastError());
        return std::make_unique<Win32Port>(h, d.inputReportBytes > 0 ? d.inputReportBytes : 65);
    }
};

}  // namespace

std::shared_ptr<HidBackend> platformHidBackend() { return std::make_shared<Win32Backend>(); }

}  // namespace vc
#endif  // _WIN32
