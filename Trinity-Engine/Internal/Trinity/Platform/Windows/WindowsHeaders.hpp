#pragma once

#include <windows.h>
#include <windowsx.h>

// windowsx.h names macros after window states, which would rename Window::IsMinimized
#undef IsMinimized
#undef IsMaximized
#undef IsRestored