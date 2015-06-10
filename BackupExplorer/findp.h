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
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

VOID BeDestroyResultNode(
    _In_ PBE_RESULT_NODE Node
    );

BOOLEAN BeFindListTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    );

NTSTATUS BeFindFilesThreadStart(
    _In_ PVOID Parameter
    );

#endif
