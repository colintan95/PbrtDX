#include "App.h"

#include <windows.h>
#include <winrt/base.h>

using winrt::check_bool;

static LRESULT WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

int WinMain(HINSTANCE hinstance, HINSTANCE, LPSTR, int cmdShow)
{
    WNDCLASSEX windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hinstance;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = L"PbrtDX";
    RegisterClassEx(&windowClass);

    RECT windowRect{};
    windowRect.left = 0;
    windowRect.top = 0;
    windowRect.right = 1024;
    windowRect.bottom = 576;

    check_bool(AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, false));

    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    HWND hwnd = CreateWindow(windowClass.lpszClassName, L"PbrtDX", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight, nullptr,
                             nullptr, hinstance, nullptr);
    ShowWindow(hwnd, cmdShow);

    App app(hwnd);

    MSG msg{};

     while (msg.message != WM_QUIT)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        app.Render();

        Sleep(16);
    }

    return 0;
}
