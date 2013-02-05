#include "explorer.h"

INT_PTR CALLBACK BeProgressDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    );

HWND BeCreateProgressDialog(
    __in HWND ParentWindowHandle
    )
{
    HWND windowHandle;

    EnableWindow(ParentWindowHandle, FALSE);

    windowHandle = CreateDialog(
        BeInstanceHandle,
        MAKEINTRESOURCE(IDD_PROGRESS),
        ParentWindowHandle,
        BeProgressDlgProc
        );
    PhCenterWindow(windowHandle, ParentWindowHandle);
    ShowWindow(windowHandle, SW_SHOW);

    return windowHandle;
}

INT_PTR CALLBACK BeProgressDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    switch (uMsg)
    {
    case BE_PROGRESS_MESSAGE_CLOSE:
        EnableWindow(GetParent(hwndDlg), TRUE);
        EndDialog(hwndDlg, IDOK);
        break;
    case BE_PROGRESS_MESSAGE_UPDATE:
        {
            HWND progressHandle = GetDlgItem(hwndDlg, IDC_PROGRESS);

            SendMessage(progressHandle, PBM_SETRANGE, 0, MAKELPARAM(0, lParam));
            SendMessage(progressHandle, PBM_SETPOS, wParam, 0);
        }
        break;
    }

    return FALSE;
}
