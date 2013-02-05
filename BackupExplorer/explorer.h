#ifndef EXPLORER_H
#define EXPLORER_H

#define PHNT_VERSION PHNT_VISTA
#include <ph.h>
#include <phgui.h>
#include <treenew.h>
#include "resource.h"

// main

extern HINSTANCE BeInstanceHandle;

// explorer

INT_PTR CALLBACK BeExplorerDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    );

// progress

#define BE_PROGRESS_MESSAGE_CLOSE (WM_APP + 1)
#define BE_PROGRESS_MESSAGE_UPDATE (WM_APP + 2)

HWND BeCreateProgressDialog(
    __in HWND ParentWindowHandle
    );

#endif
