#include "AppHost.h"
#include "TextUtil.h"
#include "WebView2Ui.h"

#include <Windows.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        MessageBoxW(
            nullptr,
            (L"COM initialization failed: " + HResultToString(comResult)).c_str(),
            L"VRec",
            MB_ICONERROR | MB_OK);
        return 1;
    }

    AppHost host;
    if (!host.Initialize()) {
        CoUninitialize();
        return 1;
    }

    WebView2Ui ui(host);
    if (!ui.Create(instance, showCommand)) {
        host.Shutdown();
        CoUninitialize();
        return 1;
    }

    const int result = ui.Run();
    host.Shutdown();
    CoUninitialize();
    return result;
}
