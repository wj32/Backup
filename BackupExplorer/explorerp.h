#ifndef EXPLORERP_H
#define EXPLORERP_H

#define BETNC_FILE 0
#define BETNC_SIZE 1
#define BETNC_BACKUPTIME 2
#define BETNC_REVISION 3
#define BETNC_TIMESTAMP 4
#define BETNC_MAXIMUM 5

typedef struct _BE_FILE_NODE
{
    PH_TREENEW_NODE Node;

    struct _BE_FILE_NODE *Parent;
    PPH_LIST Children;
    BOOLEAN IsRoot;
    BOOLEAN IsDirectory;
    BOOLEAN HasChildren;
    BOOLEAN Opened;

    PPH_STRING Name;
    LARGE_INTEGER TimeStamp;
    ULONGLONG RevisionId;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER LastBackupTime;

    PPH_STRING RevisionIdString;
    PPH_STRING TimeStampString;
    PPH_STRING EndOfFileString;
    PPH_STRING LastBackupTimeString;
} BE_FILE_NODE, *PBE_FILE_NODE;

// Revision list

BOOLEAN BeLoadRevisionList(
    VOID
    );

// File list

BOOLEAN BeFileListTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    );

PBE_FILE_NODE BeCreateFileNode(
    _In_opt_ PDB_FILE_DIRECTORY_INFORMATION Information,
    _In_opt_ PBE_FILE_NODE ParentNode
    );

VOID BeDestroyFileNode(
    _In_ PBE_FILE_NODE Node
    );

BOOLEAN BeExpandFileNode(
    _In_ PBE_FILE_NODE Node
    );

ULONG BeGetSelectedFileNodeCount(
    VOID
    );

PBE_FILE_NODE BeGetSelectedFileNode(
    VOID
    );

PPH_STRING BeComputeFullPath(
    _In_ PBE_FILE_NODE Node
    );

// File icons

typedef struct _BE_FILE_ICON_ENTRY
{
    PH_STRINGREF Key;
    PPH_STRING Extension;
    HICON Icon;
} BE_FILE_ICON_ENTRY, *PBE_FILE_ICON_ENTRY;

BOOLEAN BeFileIconEntryCompareFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    );

ULONG BeFileIconEntryHashFunction(
    _In_ PVOID Entry
    );

// Restore

typedef struct _BE_RESTORE_PARAMETERS
{
    ULONGLONG RevisionId;
    PPH_STRING FromFileName;
    PPH_STRING ToDirectoryName;
    PPH_STRING ToFileName;
} BE_RESTORE_PARAMETERS, *PBE_RESTORE_PARAMETERS;

VOID BeDestroyRestoreParameters(
    _In_ PBE_RESTORE_PARAMETERS Parameters
    );

NTSTATUS BePreviewSingleFileThreadStart(
    _In_ PVOID Parameter
    );

NTSTATUS BeRestoreFileOrDirectoryThreadStart(
    _In_ PVOID Parameter
    );

// Support functions

PPH_STRING BePromptForConfigFileName(
    VOID
    );

ULONG BeGetProgressFromMessage(
    _In_ PPH_STRINGREF Message
    );

BOOLEAN BeExecuteWithProgress(
    _In_ HWND ParentWindowHandle,
    _In_ PUSER_THREAD_START_ROUTINE ThreadStart,
    _In_opt_ PVOID Context
    );

VOID BeCompleteWithProgress(
    VOID
    );

PPH_STRING BeGetTempDirectoryName(
    VOID
    );

NTSTATUS BeIsDirectoryEmpty(
    _In_ PWSTR DirectoryName,
    _Out_ PBOOLEAN Empty
    );

#endif
