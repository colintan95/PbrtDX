#include <windows.h>

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

    HWND hwnd = CreateWindow(windowClass.lpszClassName, L"PbrtDX", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768, nullptr, nullptr, hinstance,
                             nullptr);
    ShowWindow(hwnd, cmdShow);

    MSG msg{};

     while (msg.message != WM_QUIT)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}
