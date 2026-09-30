#include "X11Window.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <chrono>
#include <cstdio>
#include <cstring>

namespace kikset::gui {

struct X11Window::Impl {
    Panel& panel;
    Display* dpy = nullptr;
    Window win = 0;
    GC gc = nullptr;
    XImage* img = nullptr;
    Graphics fb{Panel::W, Panel::H};
    bool visible = false;
    bool wasDown = false;

    explicit Impl(Panel& p) : panel(p) {}
    static double nowMs() {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }
};

X11Window::X11Window(Panel& panel, unsigned long parent) : d_(new Impl(panel)) {
    Impl& d = *d_;
    d.dpy = XOpenDisplay(nullptr);
    if (!d.dpy) return;
    const int scr = DefaultScreen(d.dpy);
    Window par = parent ? Window(parent) : RootWindow(d.dpy, scr);
    d.win = XCreateSimpleWindow(d.dpy, par, 0, 0, Panel::W, Panel::H, 0, 0, 0);
    XSelectInput(d.dpy, d.win,
                 ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
    XStoreName(d.dpy, d.win, "Kikset");
    if (!parent) {  // fixed size for a top-level window
        XSizeHints* sh = XAllocSizeHints();
        sh->flags = PMinSize | PMaxSize;
        sh->min_width = sh->max_width = Panel::W;
        sh->min_height = sh->max_height = Panel::H;
        XSetWMNormalHints(d.dpy, d.win, sh);
        XFree(sh);
    }
    d.gc = XCreateGC(d.dpy, d.win, 0, nullptr);
    d.img = XCreateImage(d.dpy, DefaultVisual(d.dpy, scr), DefaultDepth(d.dpy, scr), ZPixmap, 0,
                         reinterpret_cast<char*>(const_cast<uint32_t*>(d.fb.data())), Panel::W, Panel::H, 32, 0);
    XFlush(d.dpy);
}

X11Window::~X11Window() {
    Impl& d = *d_;
    if (d.img) { d.img->data = nullptr; XDestroyImage(d.img); }  // pixels are owned by fb
    if (d.dpy) {
        if (d.gc) XFreeGC(d.dpy, d.gc);
        if (d.win) XDestroyWindow(d.dpy, d.win);
        XCloseDisplay(d.dpy);
    }
}

bool X11Window::ok() const { return d_->dpy && d_->win && d_->img; }

void X11Window::show() {
    if (!ok()) return;
    XMapWindow(d_->dpy, d_->win);
    XFlush(d_->dpy);
    d_->visible = true;
}

void X11Window::hide() {
    if (!ok()) return;
    XUnmapWindow(d_->dpy, d_->win);
    XFlush(d_->dpy);
    d_->visible = false;
}

void X11Window::pump() {
    Impl& d = *d_;
    if (!ok()) return;
    while (XPending(d.dpy)) {
        XEvent e;
        XNextEvent(d.dpy, &e);
        const bool shift = (e.xbutton.state & ShiftMask) != 0;
        switch (e.type) {
            case ButtonPress:
                if (e.xbutton.button == 4) d.panel.wheel(e.xbutton.x, e.xbutton.y, 1, shift);
                else if (e.xbutton.button == 5) d.panel.wheel(e.xbutton.x, e.xbutton.y, -1, shift);
                else {
                    d.panel.mouseDown(e.xbutton.x, e.xbutton.y, e.xbutton.button, shift, Impl::nowMs());
                    d.wasDown = true;
                }
                break;
            case ButtonRelease:
                if (e.xbutton.button != 4 && e.xbutton.button != 5 && d.wasDown) {
                    d.panel.mouseUp();
                    d.wasDown = false;
                }
                break;
            case MotionNotify: d.panel.mouseMove(e.xmotion.x, e.xmotion.y, (e.xmotion.state & ShiftMask) != 0); break;
            default: break;
        }
    }
    if (!d.visible) return;
    d.panel.render(d.fb);
    XPutImage(d.dpy, d.win, d.gc, d.img, 0, 0, 0, 0, Panel::W, Panel::H);
    XFlush(d.dpy);
}

bool X11Window::screenshot(const char* path) {
    Impl& d = *d_;
    if (!ok()) return false;
    XSync(d.dpy, False);
    XImage* im = XGetImage(d.dpy, d.win, 0, 0, Panel::W, Panel::H, AllPlanes, ZPixmap);
    if (!im) return false;
    FILE* f = std::fopen(path, "wb");
    if (!f) { XDestroyImage(im); return false; }
    std::fprintf(f, "P6\n%d %d\n255\n", Panel::W, Panel::H);
    for (int y = 0; y < Panel::H; ++y)
        for (int x = 0; x < Panel::W; ++x) {
            const unsigned long p = XGetPixel(im, x, y);
            const unsigned char c[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p};
            std::fwrite(c, 1, 3, f);
        }
    std::fclose(f);
    XDestroyImage(im);
    return true;
}

}  // namespace kikset::gui
