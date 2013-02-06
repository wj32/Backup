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
    __in HWND hwnd,
    __in PH_TREENEW_MESSAGE Message,
    __in_opt PVOID Parameter1,
    __in_opt PVOID Parameter2,
    __in_opt PVOID Context
    );

PBE_FILE_NODE BeCreateFileNode(
    __in_opt PDB_FILE_DIRECTORY_INFORMATION Information,
    __in_opt PBE_FILE_NODE ParentNode
    );

VOID BeDestroyFileNode(
    __in PBE_FILE_NODE Node
    );

BOOLEAN BeExpandFileNode(
    __in PBE_FILE_NODE Node
    );

PBE_FILE_NODE BeGetSelectedFileNode(
    VOID
    );

PPH_STRING BeComputeFullPath(
    __in PBE_FILE_NODE Node
    );

// File icons

typedef struct _BE_FILE_ICON_ENTRY
{
    PH_STRINGREF Key;
    PPH_STRING Extension;
    HICON Icon;
} BE_FILE_ICON_ENTRY, *PBE_FILE_ICON_ENTRY;

BOOLEAN BeFileIconEntryCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    );

ULONG BeFileIconEntryHashFunction(
    __in PVOID Entry
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
    __in PBE_RESTORE_PARAMETERS Parameters
    );

NTSTATUS BePreviewSingleFileThreadStart(
    __in PVOID Parameter
    );

NTSTATUS BeRestoreFileOrDirectoryThreadStart(
    __in PVOID Parameter
    );

// Support functions

PPH_STRING BePromptForConfigFileName(
    VOID
    );

ULONG BeGetProgressFromMessage(
    __in PPH_STRINGREF Message
    );

BOOLEAN BeExecuteWithProgress(
    __in HWND ParentWindowHandle,
    __in PUSER_THREAD_START_ROUTINE ThreadStart,
    __in_opt PVOID Context
    );

VOID BeCompleteWithProgress(
    VOID
    );

PPH_STRING BeGetTempDirectoryName(
    VOID
    );

NTSTATUS BeIsDirectoryEmpty(
    __in PWSTR DirectoryName,
    __out PBOOLEAN Empty
    );

#endif
