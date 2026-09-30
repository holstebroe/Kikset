// Win32 window that shows a Panel, embedded in a host-provided HWND (CLAP win32 API,
// e.g. REAPER) or as a top-level window. Repaints itself from a WM_TIMER.
#pragma once
#include <cstdint>
#include <memory>

#include "Panel.hpp"

namespace kikset::gui {

class Win32Window {
public:
    // parent = HWND to embed into (as integer), or 0 for a top-level window.
    Win32Window(Panel& panel, uintptr_t parent);
    ~Win32Window();
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;
    bool ok() const;
    void show();
    void hide();
    void pump();  // renders a frame and schedules a repaint (also driven by the internal timer)

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace kikset::gui
