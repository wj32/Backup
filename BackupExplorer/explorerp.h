#ifndef EXPLORERP_H
#define EXPLORERP_H

#define BETNC_FILE 0
#define BETNC_SIZE 1
#define BETNC_BACKUPTIME 2

typedef struct _BE_FILE_NODE
{
    PH_TREENEW_NODE Node;

    struct _BE_FILE_NODE *Parent;
    PPH_LIST Children;
    BOOLEAN IsDirectory;
    BOOLEAN HasChildren;
    BOOLEAN Opened;

    PPH_STRING Name;
    LARGE_INTEGER TimeStamp;
    ULONGLONG RevisionId;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER LastBackupTime;
} BE_FILE_NODE, *PBE_FILE_NODE;

// Revision list

BOOLEAN BeLoadRevisionList(
    VOID
    );

BOOLEAN BeSetCurrentRevision(
    __in ULONGLONG RevisionId
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

HICON BeGetFileIconForExtension(
    __in PPH_STRINGREF Extension
    );

// Support functions

PPH_STRING BePromptForConfigFileName(
    VOID
    );

VOID BeMessageHandler(
    __in ULONG Level,
    __in __assumeRefs(1) PPH_STRING Message
    );

#endif
