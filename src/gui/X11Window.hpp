// Minimal X11 window that shows a Panel. Used embedded (parent != 0) by the CLAP GUI
// extension and stand-alone by kikset_gui_demo. Linux / X11 only.
#pragma once
#include <memory>

#include "Panel.hpp"

namespace kikset::gui {

class X11Window {
public:
    // parent = X11 window id to embed into, or 0 for a top-level window.
    X11Window(Panel& panel, unsigned long parent);
    ~X11Window();
    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;
    bool ok() const;
    void show();
    void hide();
    // Handles pending input events and repaints. Call from the GUI thread at ~30 Hz.
    void pump();
    // Reads the window contents back (for tests); returns false on failure.
    bool screenshot(const char* ppmPath);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace kikset::gui
