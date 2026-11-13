// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Pad control of the WebView2 backend. A view hosted in a window takes no synthetic input
// through its own interface, so the pointer and the keys go in through the engine's Input
// domain: the same trusted mouse and key events the real devices send, and nothing else of
// that protocol (no call that reads or scripts the page). The pointer the user sees is a
// small window of the app above the page.

#include "webview_win.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

namespace den::pal {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t POINTER_CLASS[] = L"DenWebPointer";

std::string JsonText(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += std::format("\\u{:04x}", static_cast<unsigned>(c));
        } else {
            out.push_back(c);
        }
    }
    return out + "\"";
}

struct KeyInfo {
    int code;         // the Windows virtual key
    const char* name; // DOM key and code
    const char* text; // what the key types, or null
};

KeyInfo InfoOf(WebKey key) {
    switch (key) {
    case WebKey::Enter:
        return {VK_RETURN, "Enter", "\\r"};
    case WebKey::Tab:
        return {VK_TAB, "Tab", nullptr};
    case WebKey::Backspace:
        return {VK_BACK, "Backspace", nullptr};
    case WebKey::Delete:
        return {VK_DELETE, "Delete", nullptr};
    case WebKey::Escape:
        return {VK_ESCAPE, "Escape", nullptr};
    case WebKey::Up:
        return {VK_UP, "ArrowUp", nullptr};
    case WebKey::Down:
        return {VK_DOWN, "ArrowDown", nullptr};
    case WebKey::Left:
        return {VK_LEFT, "ArrowLeft", nullptr};
    case WebKey::Right:
        return {VK_RIGHT, "ArrowRight", nullptr};
    case WebKey::PageUp:
        return {VK_PRIOR, "PageUp", nullptr};
    case WebKey::PageDown:
        return {VK_NEXT, "PageDown", nullptr};
    case WebKey::Home:
        return {VK_HOME, "Home", nullptr};
    default:
        return {VK_END, "End", nullptr};
    }
}

} // namespace

PointerWindow::~PointerWindow() {
    if (window_) {
        DestroyWindow(window_);
    }
}

// A white disc with a dark rim: readable on any page.
void PointerWindow::Paint(int size) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) {
        auto* pixels = static_cast<std::uint32_t*>(bits);
        const float centre = size * 0.5f, outer = size * 0.5f - 1.0f, inner = outer - std::max(2.0f, size * 0.14f);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float d = std::hypot(x + 0.5f - centre, y + 0.5f - centre);
                const float alpha = std::clamp(outer - d + 0.5f, 0.0f, 1.0f);
                const float white = std::clamp(inner - d + 0.5f, 0.0f, 1.0f);
                // Premultiplied: the rim is near black, the middle white.
                const auto level = static_cast<std::uint32_t>((16.0f + 239.0f * white) * alpha + 0.5f);
                const auto a = static_cast<std::uint32_t>(alpha * 255.0f + 0.5f);
                pixels[y * size + x] = (a << 24) | (level << 16) | (level << 8) | level;
            }
        }
        const HGDIOBJ old = SelectObject(memory, bitmap);
        SIZE extent{size, size};
        POINT origin{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(window_, screen, nullptr, &extent, memory, &origin, 0, &blend, ULW_ALPHA);
        SelectObject(memory, old);
    }
    if (bitmap) {
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

void PointerWindow::Show(HWND owner, int x, int y) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!window_) {
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = instance;
        cls.lpszClassName = POINTER_CLASS;
        RegisterClassW(&cls); // fails harmlessly when it is registered already
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                  POINTER_CLASS, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, nullptr);
        if (!window_) {
            return;
        }
    }
    const int size = std::max(16, MulDiv(22, static_cast<int>(GetDpiForWindow(owner)), 96));
    if (size != size_) {
        size_ = size;
        Paint(size);
    }
    SetWindowPos(window_, HWND_TOP, x - size / 2, y - size / 2, size, size, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void PointerWindow::Hide() {
    if (window_) {
        ShowWindow(window_, SW_HIDE);
    }
}

double WinWebView::PixelsPerDip() const {
    ComPtr<ICoreWebView2Controller3> controller3;
    double scale = 0;
    if (controller_ && SUCCEEDED(controller_.As(&controller3)) &&
        SUCCEEDED(controller3->get_RasterizationScale(&scale)) && scale > 0) {
        return scale;
    }
    return GetDpiForWindow(parent_) / 96.0;
}

// Positions go in as the engine counts them: device-independent pixels of the view.
void WinWebView::Mouse(const char* type, int x, int y, const std::string& extra) {
    if (!view_) {
        return;
    }
    const double scale = PixelsPerDip();
    const std::string json = std::format("{{\"type\":\"{}\",\"x\":{:.2f},\"y\":{:.2f}{}}}", type, x / scale,
                                         y / scale, extra);
    view_->CallDevToolsProtocolMethod(L"Input.dispatchMouseEvent", WebWide(json).c_str(), nullptr);
}

void WinWebView::PlacePointer() {
    const bool shown = pointer_visible_ && visible_ && pointer_x_ >= 0 && !IsIconic(parent_);
    if (!shown) {
        pointer_.Hide();
        return;
    }
    POINT point{bounds_.x + pointer_x_, bounds_.y + pointer_y_};
    ClientToScreen(parent_, &point);
    pointer_.Show(parent_, point.x, point.y);
}

void WinWebView::SetPointerVisible(bool visible) {
    pointer_visible_ = visible;
    PlacePointer();
}

void WinWebView::MovePointer(int x, int y) {
    x = std::clamp(x, 0, std::max(0, bounds_.w - 1));
    y = std::clamp(y, 0, std::max(0, bounds_.h - 1));
    if (x != pointer_x_ || y != pointer_y_) {
        pointer_x_ = x;
        pointer_y_ = y;
        Mouse("mouseMoved", x, y, {});
    }
    // Also when nothing moved: the app's window may have.
    PlacePointer();
}

void WinWebView::Click(int x, int y) {
    MovePointer(x, y);
    if (controller_) {
        // The page gets the keyboard, as after a real click.
        controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
    Mouse("mousePressed", pointer_x_, pointer_y_, ",\"button\":\"left\",\"buttons\":1,\"clickCount\":1");
    Mouse("mouseReleased", pointer_x_, pointer_y_, ",\"button\":\"left\",\"buttons\":0,\"clickCount\":1");
}

void WinWebView::Scroll(int x, int y, int dx, int dy) {
    const double scale = PixelsPerDip();
    Mouse("mouseWheel", x, y, std::format(",\"deltaX\":{:.2f},\"deltaY\":{:.2f}", dx / scale, dy / scale));
}

void WinWebView::TypeText(const std::string& text) {
    if (!view_ || text.empty()) {
        return;
    }
    const std::string json = "{\"text\":" + JsonText(text) + "}";
    view_->CallDevToolsProtocolMethod(L"Input.insertText", WebWide(json).c_str(), nullptr);
}

void WinWebView::Key(WebKey key) {
    if (!view_) {
        return;
    }
    const KeyInfo info = InfoOf(key);
    const std::string common = std::format("\"windowsVirtualKeyCode\":{},\"nativeVirtualKeyCode\":{},\"key\":\"{}\",\"code\":\"{}\"",
                                           info.code, info.code, info.name, info.name);
    const std::string down = info.text ? std::format("{{\"type\":\"keyDown\",\"text\":\"{}\",{}}}", info.text, common)
                                       : std::format("{{\"type\":\"rawKeyDown\",{}}}", common);
    view_->CallDevToolsProtocolMethod(L"Input.dispatchKeyEvent", WebWide(down).c_str(), nullptr);
    const std::string up = std::format("{{\"type\":\"keyUp\",{}}}", common);
    view_->CallDevToolsProtocolMethod(L"Input.dispatchKeyEvent", WebWide(up).c_str(), nullptr);
}

} // namespace den::pal
