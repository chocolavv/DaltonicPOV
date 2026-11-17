// Resource ids of app.rc, and the icon drawn from icon.svg.
#pragma once
#define ID_NAME 1
#define ID_NORMAL 2
#define ID_EXIT 3
#define ID_ICON_SVG 4

#ifndef RC_INVOKED
#include <windows.h>
bool dp_iconPixels(int px, void* out);  // top-down premultiplied BGRA, px * px
HICON dp_icon(int px);                  // needs COM initialized on the thread
#endif
