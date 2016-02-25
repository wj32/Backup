#ifndef EXPLORER_H
#define EXPLORER_H

#define PHNT_VERSION PHNT_VISTA
#include <ph.h>
#include <guisup.h>
#include <filestream.h>
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
extern PPH_STRING BeFileName;

LONG BeMessageLoop(
    VOID
    );

VOID BeRegisterDialog(
    _In_ HWND DialogWindowHandle
    );

VOID BeUnregisterDialog(
    _In_ HWND DialogWindowHandle
    );

// explorer

extern HWND BeWindowHandle;
extern PBK_CONFIG BeConfig;

INT_PTR CALLBACK BeExplorerDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

BOOLEAN BeSetCurrentRevision(
    _In_ ULONGLONG RevisionId
    );

VOID BeSelectFullPath(
    _In_ PPH_STRINGREF FullPath
    );

HICON BeGetFileIconForExtension(
    _In_ PPH_STRINGREF Extension
    );

VOID BePreviewSingleFileWithProgress(
    _In_ HWND ParentWindowHandle,
    _In_ ULONGLONG RevisionId,
    _In_ PPH_STRINGREF FileName
    );

VOID BeRestoreFileOrDirectoryWithProgress(
    _In_ HWND ParentWindowHandle,
    _In_ ULONGLONG RevisionId,
    _In_ PPH_STRINGREF FileName,
    _In_ BOOLEAN Directory
    );

VOID BeMessageHandler(
    _In_ ULONG Level,
    _In_ _Assume_refs_(1) PPH_STRING Message
    );

BOOLEAN PhGetListViewContextMenuPoint(
    _In_ HWND ListViewHandle,
    _Out_ PPOINT Point
    );

// progress

#define BE_PROGRESS_MESSAGE_CLOSE (WM_APP + 1)
#define BE_PROGRESS_MESSAGE_UPDATE (WM_APP + 2)

HWND BeCreateProgressDialog(
    _In_ HWND ParentWindowHandle
    );

// revisions

VOID BeShowRevisionsDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_STRINGREF FileName
    );

// find

VOID BeShowFindDialog(
    VOID
    );

#endif
