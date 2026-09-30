// Picks the OS window implementation and the matching CLAP window API.
#pragma once
#if defined(KIKSET_HAS_X11)
#include "X11Window.hpp"
#define KIKSET_HAS_GUI 1
#define KIKSET_CLAP_WINDOW_API CLAP_WINDOW_API_X11
namespace kikset::gui { using PlatformWindow = X11Window; }
#elif defined(KIKSET_HAS_WIN32)
#include "Win32Window.hpp"
#define KIKSET_HAS_GUI 1
#define KIKSET_CLAP_WINDOW_API CLAP_WINDOW_API_WIN32
namespace kikset::gui { using PlatformWindow = Win32Window; }
#endif
