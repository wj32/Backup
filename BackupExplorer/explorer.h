#ifndef EXPLORER_H
#define EXPLORER_H

#define PHNT_VERSION PHNT_VISTA
#include <ph.h>
#include <phgui.h>
#include <treenew.h>
#include "resource.h"

// explorer

INT_PTR CALLBACK BeExplorerDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    );

#endif
