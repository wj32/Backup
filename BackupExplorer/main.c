#include "explorer.h"

HINSTANCE BeInstanceHandle;

static PPH_LIST DialogList = NULL;

INT WINAPI WinMain(
    __in HINSTANCE hInstance,
    __in_opt HINSTANCE hPrevInstance,
    __in LPSTR lpCmdLine,
    __in INT nCmdShow
    )
{
    BeInstanceHandle = hInstance;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    PhInitializePhLibEx(PHLIB_INIT_MODULE_WORK_QUEUE | PHLIB_INIT_MODULE_IO_SUPPORT, 512 * 1024, 16 * 1024);

    PhApplicationName = L"Backup Explorer";

    PhGuiSupportInitialization();
    PhTreeNewInitialization();

    BeWindowHandle = CreateDialog(
        hInstance,
        MAKEINTRESOURCE(IDD_EXPLORER),
        NULL,
        BeExplorerDlgProc
        );
    BeRegisterDialog(BeWindowHandle);
    ShowWindow(BeWindowHandle, SW_SHOW);

    return BeMessageLoop();
}

LONG BeMessageLoop(
    VOID
    )
{
    BOOL result;
    MSG message;
    HACCEL acceleratorTable;

    acceleratorTable = LoadAccelerators(BeInstanceHandle, MAKEINTRESOURCE(IDR_MAIN_ACCEL));

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        BOOLEAN processed = FALSE;
        ULONG i;

        if (result == -1)
            return 1;

        if (
            message.hwnd == BeWindowHandle ||
            IsChild(BeWindowHandle, message.hwnd)
            )
        {
            if (TranslateAccelerator(BeWindowHandle, acceleratorTable, &message))
                processed = TRUE;
        }

        if (DialogList)
        {
            for (i = 0; i < DialogList->Count; i++)
            {
                if (IsDialogMessage((HWND)DialogList->Items[i], &message))
                {
                    processed = TRUE;
                    break;
                }
            }
        }

        if (!processed)
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }

    return (LONG)message.wParam;
}

// ----
// Copied from Process Hacker's main.c

VOID BeRegisterDialog(
    __in HWND DialogWindowHandle
    )
{
    if (!DialogList)
        DialogList = PhCreateList(2);

    PhAddItemList(DialogList, (PVOID)DialogWindowHandle);
}

VOID BeUnregisterDialog(
    __in HWND DialogWindowHandle
    )
{
    ULONG indexOfDialog;

    if (!DialogList)
        return;

    indexOfDialog = PhFindItemList(DialogList, (PVOID)DialogWindowHandle);

    if (indexOfDialog != -1)
        PhRemoveItemList(DialogList, indexOfDialog);
}

// ----
