#define NOMINMAX
#include <windows.h>

namespace {

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CLOSE:
        // Mirror Amnezia: closing the window does not quit the process.
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wParam && (lParam & ENDSESSION_CLOSEAPP)) {
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace

int main()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"AmneziaVpnUpdaterTestWindow";

    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 2;

    const HWND window = CreateWindowExW(
        0,
        windowClass.lpszClassName,
        L"AmneziaVPN test process",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 320, 160,
        nullptr, nullptr, instance, nullptr);

    if (!window)
        return 3;

    ShowWindow(window, SW_SHOW);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return 0;
}
