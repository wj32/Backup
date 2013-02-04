#include "explorer.h"

INT WINAPI WinMain(
    __in HINSTANCE hInstance,
    __in_opt HINSTANCE hPrevInstance,
    __in LPSTR lpCmdLine,
    __in INT nCmdShow
    )
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    PhInitializePhLibEx(PHLIB_INIT_MODULE_WORK_QUEUE | PHLIB_INIT_MODULE_IO_SUPPORT, 512 * 1024, 16 * 1024);
    PhGuiSupportInitialization();
    PhTreeNewInitialization();

    DialogBox(
        hInstance,
        MAKEINTRESOURCE(IDD_EXPLORER),
        NULL,
        BeExplorerDlgProc
        );

    return 0;
}
