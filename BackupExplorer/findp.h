#ifndef FINDP_H
#define FINDP_H

#define BEFTNC_FILE 0
#define BEFTNC_SIZE 1
#define BEFTNC_BACKUPTIME 2
#define BEFTNC_LASTREVISION 3
#define BEFTNC_MAXIMUM 4

typedef struct _BE_RESULT_NODE
{
    PH_TREENEW_NODE Node;

    PPH_STRING FileName;
    BOOLEAN IsDirectory;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER LastBackupTime;
    ULONGLONG LastRevisionId;

    PPH_STRING EndOfFileString;
    PPH_STRING LastBackupTimeString;
    PPH_STRING LastRevisionIdString;
} BE_RESULT_NODE, *PBE_RESULT_NODE;

INT_PTR CALLBACK BeFindDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    );

VOID BeDestroyResultNode(
    __in PBE_RESULT_NODE Node
    );

BOOLEAN BeFindListTreeNewCallback(
    __in HWND hwnd,
    __in PH_TREENEW_MESSAGE Message,
    __in_opt PVOID Parameter1,
    __in_opt PVOID Parameter2,
    __in_opt PVOID Context
    );

NTSTATUS BeFindFilesThreadStart(
    __in PVOID Parameter
    );

#endif
