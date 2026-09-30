#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Win32Window.hpp"

#include <windows.h>
#include <windowsx.h>

namespace kikset::gui {

namespace {
const wchar_t* kClassName = L"KiksetEditorWindow";
constexpr UINT_PTR kTimerId = 1;

HMODULE thisModule() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&thisModule), &m);
    return m;
}
}  // namespace

struct Win32Window::Impl {
    Panel& panel;
    HWND hwnd = nullptr;
    Graphics fb{Panel::W, Panel::H};
    BITMAPINFO bmi{};
    bool dragging = false;

    explicit Impl(Panel& p) : panel(p) {
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = Panel::W;
        bmi.bmiHeader.biHeight = -Panel::H;  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
    }

    void paint(HDC dc) {
        SetDIBitsToDevice(dc, 0, 0, Panel::W, Panel::H, 0, 0, 0, Panel::H, fb.data(), &bmi, DIB_RGB_COLORS);
    }

    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto* d = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!d) return DefWindowProcW(h, msg, wp, lp);
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        switch (msg) {
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(h, &ps);
                d->paint(dc);
                EndPaint(h, &ps);
                return 0;
            }
            case WM_TIMER:
                if (wp == kTimerId) {
                    d->panel.render(d->fb);
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
                SetFocus(h);
                SetCapture(h);
                d->dragging = true;
                d->panel.mouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), msg == WM_LBUTTONDOWN ? 1 : 3, shift,
                                   double(GetTickCount64()));
                return 0;
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
                if (d->dragging) {
                    d->panel.mouseUp();
                    d->dragging = false;
                    ReleaseCapture();
                }
                return 0;
            case WM_MOUSEMOVE:
                d->panel.mouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), shift);
                return 0;
            case WM_MOUSEWHEEL: {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(h, &pt);
                d->panel.wheel(pt.x, pt.y, double(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA, shift);
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (d->dragging) { d->panel.mouseUp(); d->dragging = false; }
                return 0;
            default: return DefWindowProcW(h, msg, wp, lp);
        }
    }
};

Win32Window::Win32Window(Panel& panel, uintptr_t parent) : d_(new Impl(panel)) {
    Impl& d = *d_;
    HINSTANCE inst = thisModule();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &Impl::proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);  // fails harmlessly if it already exists

    if (parent) {
        d.hwnd = CreateWindowExW(0, kClassName, L"Kikset", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, Panel::W, Panel::H,
                                 reinterpret_cast<HWND>(parent), nullptr, inst, nullptr);
    } else {
        RECT r{0, 0, Panel::W, Panel::H};
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRect(&r, style, FALSE);
        d.hwnd = CreateWindowExW(0, kClassName, L"Kikset", style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                                 r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    }
    if (d.hwnd) {
        SetWindowLongPtrW(d.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&d));
        d.panel.render(d.fb);
    }
}

Win32Window::~Win32Window() {
    Impl& d = *d_;
    if (d.hwnd) {
        KillTimer(d.hwnd, kTimerId);
        SetWindowLongPtrW(d.hwnd, GWLP_USERDATA, 0);
        DestroyWindow(d.hwnd);
    }
}

bool Win32Window::ok() const { return d_->hwnd != nullptr; }

void Win32Window::show() {
    if (!ok()) return;
    ShowWindow(d_->hwnd, SW_SHOW);
    SetTimer(d_->hwnd, kTimerId, 33, nullptr);
    pump();
}

void Win32Window::hide() {
    if (!ok()) return;
    KillTimer(d_->hwnd, kTimerId);
    ShowWindow(d_->hwnd, SW_HIDE);
}

void Win32Window::pump() {
    if (!ok()) return;
    d_->panel.render(d_->fb);
    InvalidateRect(d_->hwnd, nullptr, FALSE);
}

}  // namespace kikset::gui
