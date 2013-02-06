#ifndef EXPLORER_H
#define EXPLORER_H

#define PHNT_VERSION PHNT_VISTA
#include <ph.h>
#include <phgui.h>
#include <treenew.h>
#include "resource.h"

#include "../Backup/config.h"
#include "../Backup/db.h"
#include "../Backup/vssobj.h"
#include "../Backup/package.h"
#include "../Backup/engine.h"
#include "../Backup/enginep.h"

// main

extern HINSTANCE BeInstanceHandle;

// explorer

extern HWND BeWindowHandle;
extern PBK_CONFIG BeConfig;

INT_PTR CALLBACK BeExplorerDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    );

BOOLEAN BeSetCurrentRevision(
    __in ULONGLONG RevisionId
    );

VOID BeSelectFullPath(
    __in PPH_STRINGREF FullPath
    );

HICON BeGetFileIconForExtension(
    __in PPH_STRINGREF Extension
    );

VOID BePreviewSingleFileWithProgress(
    __in HWND ParentWindowHandle,
    __in ULONGLONG RevisionId,
    __in PPH_STRINGREF FileName
    );

VOID BeRestoreFileOrDirectoryWithProgress(
    __in HWND ParentWindowHandle,
    __in ULONGLONG RevisionId,
    __in PPH_STRINGREF FileName,
    __in BOOLEAN Directory
    );

VOID BeMessageHandler(
    __in ULONG Level,
    __in __assumeRefs(1) PPH_STRING Message
    );

BOOLEAN PhGetListViewContextMenuPoint(
    __in HWND ListViewHandle,
    __out PPOINT Point
    );

// progress

#define BE_PROGRESS_MESSAGE_CLOSE (WM_APP + 1)
#define BE_PROGRESS_MESSAGE_UPDATE (WM_APP + 2)

HWND BeCreateProgressDialog(
    __in HWND ParentWindowHandle
    );

// revisions

VOID BeShowRevisionsDialog(
    __in HWND ParentWindowHandle,
    __in PPH_STRINGREF FileName
    );

// find

VOID BeShowFindDialog(
    VOID
    );

#endif
