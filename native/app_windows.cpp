// Tray app: the whole screen as is, or through one plugin from the plugins folder beside the exe.
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "app.h"
#include "filter.h"
#include "plugin.h"

namespace {
constexpr UINT kTray = WM_APP + 1, kShowMenu = WM_APP + 2, kFirstPlugin = 100;
constexpr wchar_t kClass[] = L"DaltonicPOV";
UINT taskbarCreated;
std::vector<const dp_plugin*> plugins;
int mode = -1;  // -1 the screen as is, else an index into plugins
NOTIFYICONDATAW icon{sizeof(icon)};

void loadPlugins() {  // every DLL in <exe dir>\plugins; loaded for the app's whole life
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L'\\')) + L"\\plugins\\";
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"*.dll").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        HMODULE dll = LoadLibraryW((dir + fd.cFileName).c_str());
        auto get = dll ? (dp_plugins_fn)GetProcAddress(dll, DP_PLUGINS_EXPORT) : nullptr;
        for (auto p = get ? get() : nullptr; p && *p; p++)
            if ((*p)->version == DP_PLUGIN_VERSION) plugins.push_back(*p);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

std::wstring text(UINT id) {
    const wchar_t* p = nullptr;
    int n = LoadStringW(GetModuleHandleW(nullptr), id, (LPWSTR)&p, 0);
    return {p, (size_t)n};
}

int envInt(const wchar_t* name, int otherwise) {
    wchar_t v[16];
    return GetEnvironmentVariableW(name, v, 16) ? _wtoi(v) : otherwise;
}

// For tests: DALTONICPOV_SCREEN limits the filter to one output, DALTONICPOV_MARK=1 shows it to capture.
void setMode(int m) {
    dp::stop();
    mode = m;
    if (m >= 0) dp::start(plugins[m], envInt(L"DALTONICPOV_SCREEN", -1), envInt(L"DALTONICPOV_MARK", 0));
}

void command(HWND h, int cmd) {  // a menu pick, or the same WM_COMMAND from a test
    if (cmd == ID_EXIT) DestroyWindow(h);
    else if (cmd >= (int)kFirstPlugin - 1 && cmd < (int)(kFirstPlugin + plugins.size())) setMode(cmd - kFirstPlugin);
}

void showMenu(HWND h) {
    HMENU menu = CreatePopupMenu();
    auto item = [&](int m, const wchar_t* label) {
        AppendMenuW(menu, MF_STRING | MFT_RADIOCHECK | (m == mode ? MF_CHECKED : 0), kFirstPlugin + m, label);
    };
    item(-1, text(ID_NORMAL).c_str());
    for (int m = 0; m < (int)plugins.size(); m++) item(m, plugins[m]->name());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, text(ID_EXIT).c_str());
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(h);  // without it the menu stays open after a click outside it
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
    DestroyMenu(menu);
    if (cmd) command(h, cmd);
}

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case kTray:
        if (l == WM_LBUTTONUP || l == WM_RBUTTONUP) showMenu(h);
        return 0;
    case kShowMenu:
        showMenu(h);
        return 0;
    case WM_COMMAND:
        command(h, LOWORD(w));
        return 0;
    case WM_DISPLAYCHANGE:  // a monitor came or went: start on the new set
        if (mode >= 0) setMode(mode);
        return 0;
    case WM_DESTROY:
        dp::stop();
        Shell_NotifyIconW(NIM_DELETE, &icon);
        PostQuitMessage(0);
        return 0;
    }
    if (m == taskbarCreated) Shell_NotifyIconW(NIM_ADD, &icon);  // Explorer restarted
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CreateMutexW(nullptr, TRUE, kClass);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {  // a second start opens the first one's menu
        if (HWND first = FindWindowW(kClass, nullptr)) {
            DWORD pid;
            GetWindowThreadProcessId(first, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(first, kShowMenu, 0, 0);
        }
        return 0;
    }
    loadPlugins();
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc, wc.hInstance = inst, wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, kClass, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    icon.hWnd = h, icon.uID = 1, icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP, icon.uCallbackMessage = kTray;
    icon.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION
    wcsncpy_s(icon.szTip, text(ID_NAME).c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &icon);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    return 0;
}
