#ifndef ENGINEP_H
#define ENGINEP_H

typedef enum _EN_FILEINFO_STATE
{
    FileInfoPreEnum,
    FileInfoEnum,
    FileInfoPostEnum
} EN_FILEINFO_STATE;

#define EN_DIFF_SWITCHED 0x1 // this file or a parent was switched from a file to a directory

typedef struct _EN_FILEINFO
{
    SINGLE_LIST_ENTRY ListEntry;
    struct _EN_FILEINFO *Parent;
    BOOLEAN Directory;
    BOOLEAN FsExpand; // fill in file list from file system
    BOOLEAN NeedFsInfo; // fill in FileInformation
    UCHAR DiffFlags; // inherited diff flags
    PH_STRINGREF Key;
    PPH_STRING Name; // example.txt
    PPH_STRING FullFileName; // mapping:\folder\example.txt (file name in database)
    PPH_STRING FullSourceFileName; // D:\folder\example.txt (file name in file system)
    FILE_NETWORK_OPEN_INFORMATION FileInformation;

    PPH_HASHTABLE Files;

    // State
    PH_HASHTABLE_ENUM_CONTEXT EnumContext;
    EN_FILEINFO_STATE State;
    PDBF_FILE DbFile;
    PPH_FILE_STREAM FileStream;
    BOOLEAN FileStreamAttempted;
} EN_FILEINFO, *PEN_FILEINFO;

typedef struct _EN_PACKAGE_CALLBACK_CONTEXT
{
    PBK_CONFIG Config;
    PDB_DATABASE Database;
    PBK_VSS_OBJECT Vss;
    PEN_MESSAGE_HANDLER MessageHandler;

    union
    {
        struct
        {
            PPH_HASHTABLE IgnoreFileNames;
        } Merge;
        struct
        {
            ULONG Flags;
            PPH_STRINGREF RestoreToDirectory;
            PPH_STRINGREF RestoreToName;

            PPH_STRINGREF BaseFileName;
            PPH_HASHTABLE FileNames;
        } Restore;
    };
} EN_PACKAGE_CALLBACK_CONTEXT, *PEN_PACKAGE_CALLBACK_CONTEXT;

typedef struct _EN_REVISION_ENTRY
{
    ULONGLONG RevisionId;
    PPH_HASHTABLE FileNames;
    PPH_HASHTABLE DirectoryNames;
} EN_REVISION_ENTRY, *PEN_REVISION_ENTRY;

// Backup

NTSTATUS EnpBackupFirstRevision(
    _In_ PBK_CONFIG Config,
    _In_opt_ HANDLE TransactionHandle,
    _In_ PDB_DATABASE Database,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpSyncTreeFirstRevision(
    _In_ PBK_CONFIG Config,
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE Directory,
    _Inout_ PEN_FILEINFO Root,
    _In_ PPK_ACTION_LIST ActionList,
    _In_opt_ PBK_VSS_OBJECT Vss,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpSyncFileFirstRevision(
    _In_ PBK_CONFIG Config,
    _In_ PDB_DATABASE Database,
    _In_ PEN_FILEINFO FileInfo,
    _In_ PPK_ACTION_LIST ActionList,
    _In_opt_ PBK_VSS_OBJECT Vss,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpBackupNewRevision(
    _In_ PBK_CONFIG Config,
    _In_opt_ HANDLE TransactionHandle,
    _In_ PDB_DATABASE Database,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpTestBackupNewRevision(
    _In_ PBK_CONFIG Config,
    _In_ PDB_DATABASE Database,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffTreeNewRevision(
    _In_ PBK_CONFIG Config,
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG NewRevisionId,
    _In_ PDBF_FILE HeadDirectory,
    _In_opt_ PDBF_FILE DiffDirectory,
    _Inout_ PEN_FILEINFO Root,
    _In_opt_ PPK_ACTION_LIST ActionList,
    _Inout_ PULONGLONG NumberOfChanges,
    _In_opt_ PBK_VSS_OBJECT Vss,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffDirectoryNewRevision(
    _In_ PBK_CONFIG Config,
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG NewRevisionId,
    _In_ PDBF_FILE HeadDirectory,
    _In_opt_ PDBF_FILE DiffDirectory,
    _In_ PEN_FILEINFO FileInfo,
    _In_opt_ PPK_ACTION_LIST ActionList,
    _Inout_ PULONGLONG NumberOfChanges,
    _In_opt_ PBK_VSS_OBJECT Vss,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffAddFileNewRevision(
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG NewRevisionId,
    _In_ PDBF_FILE HeadDirectory,
    _In_ PDBF_FILE DiffDirectory,
    _In_ PDBF_FILE ThisDirectoryInHead,
    _In_ PEN_FILEINFO FileInfo,
    _In_ BOOLEAN CreateDiffFile,
    _In_ PPK_ACTION_LIST ActionList,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffDeleteFileNewRevision(
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG NewRevisionId,
    _In_ PDBF_FILE HeadDirectory,
    _In_ PDBF_FILE DiffDirectory,
    _In_ PDBF_FILE ThisDirectoryInHead,
    _In_ PEN_FILEINFO DirectoryFileInfo,
    _In_ PDB_FILE_DIRECTORY_INFORMATION Entry,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffModifyFileNewRevision(
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG NewRevisionId,
    _In_ PDBF_FILE HeadDirectory,
    _In_ PDBF_FILE DiffDirectory,
    _In_ PDBF_FILE ThisDirectoryInHead,
    _In_ PEN_FILEINFO FileInfo,
    _In_ PDB_FILE_DIRECTORY_INFORMATION Entry,
    _In_ PPK_ACTION_LIST ActionList,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

BOOLEAN EnpDirectoryEntryCompareFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    );

ULONG EnpDirectoryEntryHashFunction(
    _In_ PVOID Entry
    );

PDB_FILE_DIRECTORY_INFORMATION EnpFindDirectoryEntry(
    _In_ PPH_HASHTABLE Hashtable,
    _In_ PPH_STRING Name
    );

HRESULT EnpBackupPackageCallback(
    _In_ PK_PACKAGE_CALLBACK_MESSAGE Message,
    _In_opt_ PPK_ACTION Action,
    _In_ PVOID Parameter,
    _In_opt_ PVOID Context
    );

HRESULT EnpOpenStreamForFile(
    _In_ PEN_FILEINFO FileInfo,
    _In_opt_ PBK_VSS_OBJECT Vss,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

// Merge

NTSTATUS EnpRevertToRevision(
    _In_ PBK_CONFIG Config,
    _In_opt_ HANDLE TransactionHandle,
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG TargetRevisionId,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpMergeDirectoryToHead(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE DirectoryInHead,
    _In_ PDBF_FILE DirectoryInDiff,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpTrimToRevision(
    _In_ PBK_CONFIG Config,
    _In_opt_ HANDLE TransactionHandle,
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG TargetFirstRevisionId,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpAddMergeFileNamesFromDirectory(
    _In_ PDB_DATABASE Database,
    _In_ PPH_HASHTABLE RevisionEntries,
    _In_ PDBF_FILE Directory,
    _In_opt_ PPH_STRING DirectoryName
    );

NTSTATUS EnpMergePackages(
    _In_ PBK_CONFIG Config,
    _In_opt_ HANDLE TransactionHandle,
    _In_ ULONGLONG OldFirstRevisionId,
    _In_ ULONGLONG NewFirstRevisionId,
    _In_ PPH_HASHTABLE RevisionEntries,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

HRESULT EnpMergePackageCallback(
    _In_ PK_PACKAGE_CALLBACK_MESSAGE Message,
    _In_opt_ PPK_ACTION Action,
    _In_ PVOID Parameter,
    _In_opt_ PVOID Context
    );

NTSTATUS EnpUpdateDatabaseAfterTrim(
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG OldFirstRevisionId,
    _In_ ULONGLONG NewFirstRevisionId,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpUpdateDirectoryMinimumRevisionIds(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE Directory,
    _In_ ULONGLONG FirstRevisionId
    );

BOOLEAN EnpRevisionEntryCompareFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    );

ULONG EnpRevisionEntryHashFunction(
    _In_ PVOID Entry
    );

// Restore

NTSTATUS EnpRestoreFromRevision(
    _In_ PBK_CONFIG Config,
    _In_ PDB_DATABASE Database,
    _In_ ULONG Flags,
    _In_ PPH_STRINGREF FileName,
    _In_opt_ ULONGLONG RevisionId,
    _In_ PPH_STRINGREF RestoreToDirectory,
    _In_opt_ PPH_STRINGREF RestoreToName,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpRestoreSingleFileFromRevision(
    _In_ PBK_CONFIG Config,
    _In_ PDB_DATABASE Database,
    _In_ ULONG Flags,
    _In_ PPH_STRINGREF FileName,
    _In_ ULONGLONG RevisionId,
    _In_ PPH_STRINGREF RestoreToDirectory,
    _In_opt_ PPH_STRINGREF RestoreToName,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpRestoreDirectoryFromHead(
    _In_ PBK_CONFIG Config,
    _In_ PDB_DATABASE Database,
    _In_ ULONG Flags,
    _In_ PPH_STRINGREF FileName,
    _In_ PPH_STRINGREF RestoreToDirectory,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpAddRestoreFileNamesFromDirectory(
    _In_ PDB_DATABASE Database,
    _In_ PPH_HASHTABLE RevisionEntries,
    _In_ PDBF_FILE Directory,
    _In_ PPH_STRING DirectoryName
    );

NTSTATUS EnpRestoreDirectoryFromRevision(
    _In_ PBK_CONFIG Config,
    _In_ ULONG Flags,
    _In_ PPH_STRINGREF FileName,
    _In_ ULONGLONG RevisionId,
    _In_ PPH_STRINGREF RestoreToDirectory,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpMergeToHeadUntilRevision(
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG TargetRevisionId,
    _In_ PEN_MESSAGE_HANDLER MessageHandler,
    _Out_opt_ PDBF_FILE *HeadDirectory
    );

NTSTATUS EnpExtractFromPackage(
    _In_ PBK_CONFIG Config,
    _In_ ULONG Flags,
    _In_ ULONGLONG RevisionId,
    _In_opt_ PPH_STRINGREF BaseFileName,
    _In_ PPH_HASHTABLE FileNames,
    _In_ PPH_STRINGREF RestoreToDirectory,
    _In_opt_ PPH_STRINGREF RestoreToName,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

HRESULT EnpRestorePackageCallback(
    _In_ PK_PACKAGE_CALLBACK_MESSAGE Message,
    _In_opt_ PPK_ACTION Action,
    _In_ PVOID Parameter,
    _In_opt_ PVOID Context
    );

NTSTATUS EnpQueryFileRevisions(
    _In_ PDB_DATABASE Database,
    _In_ PPH_STRINGREF FileName,
    _In_ ULONGLONG FirstRevisionId,
    _In_ ULONGLONG LastRevisionId,
    _Out_ PEN_FILE_REVISION_INFORMATION *Entries,
    _Out_ PULONG NumberOfEntries,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

// Other

NTSTATUS EnpCompareRevisions(
    _In_ PBK_CONFIG Config,
    _In_ PDB_DATABASE Database,
    _In_ ULONGLONG BaseRevisionId,
    _In_opt_ ULONGLONG TargetRevisionId,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffDirectoryCompareRevisions(
    _In_opt_ PDB_DATABASE BaseDatabase,
    _In_ PDB_DATABASE TargetDatabase,
    _In_opt_ PDBF_FILE BaseDirectory,
    _In_ PDBF_FILE TargetDirectory,
    _In_ PPH_STRING FileName,
    _Inout_ PULONGLONG NumberOfChanges,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

// File names

PPH_HASHTABLE EnpCreateFileNameHashtable(
    VOID
    );

VOID EnpDestroyFileNameHashtable(
    _In_ PPH_HASHTABLE Hashtable
    );

VOID EnpAddToFileNameHashtable(
    _In_ PPH_HASHTABLE Hashtable,
    _In_ PPH_STRING FileName
    );

PPH_STRING EnpFindInFileNameHashtable(
    _In_ PPH_HASHTABLE Hashtable,
    _In_ PPH_STRINGREF FileName
    );

BOOLEAN EnpRemoveFromFileNameHashtable(
    _In_ PPH_HASHTABLE Hashtable,
    _In_ PPH_STRINGREF FileName
    );

BOOLEAN EnpFileNameCompareFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    );

ULONG EnpFileNameHashFunction(
    _In_ PVOID Entry
    );

// File info

typedef struct _EN_POPULATE_FS_CONTEXT
{
    PBK_CONFIG Config;
    PEN_FILEINFO FileInfo;
} EN_POPULATE_FS_CONTEXT, *PEN_POPULATE_FS_CONTEXT;

PEN_FILEINFO EnpCreateFileInfo(
    _In_opt_ PEN_FILEINFO Parent,
    _In_opt_ PPH_STRINGREF Name,
    _In_opt_ PPH_STRINGREF SourceName
    );

BOOLEAN EnpFileInfoCompareFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    );

ULONG EnpFileInfoHashFunction(
    _In_ PVOID Entry
    );

VOID EnpCreateHashtableFileInfo(
    _Inout_ PEN_FILEINFO FileInfo
    );

VOID EnpDestroyFileInfo(
    _In_ PEN_FILEINFO FileInfo
    );

VOID EnpClearFileInfo(
    _In_ PEN_FILEINFO FileInfo
    );

VOID EnpFreeFileInfo(
    _In_ PEN_FILEINFO FileInfo
    );

PEN_FILEINFO EnpFindFileInfo(
    _In_ PEN_FILEINFO FileInfo,
    _In_ PPH_STRINGREF Name
    );

PEN_FILEINFO EnpCreateRootFileInfo(
    VOID
    );

VOID EnpMapBaseNamesFileInfo(
    _In_ PBK_CONFIG Config,
    _Inout_ PEN_FILEINFO FileInfo
    );

VOID EnpAddSourceToRoot(
    _In_ PBK_CONFIG Config,
    _In_ PEN_FILEINFO Root,
    _In_ PPH_STRINGREF SourceFileName,
    _In_ BOOLEAN Directory
    );

VOID EnpPopulateRootFileInfo(
    _In_ PBK_CONFIG Config,
    _Inout_ PEN_FILEINFO Root
    );

BOOLEAN NTAPI EnpEnumDirectoryFile(
    _In_ PFILE_DIRECTORY_INFORMATION Information,
    _In_opt_ PVOID Context
    );

NTSTATUS EnpPopulateFsFileInfo(
    _In_ PBK_CONFIG Config,
    _Inout_ PEN_FILEINFO FileInfo,
    _In_opt_ PBK_VSS_OBJECT Vss
    );

NTSTATUS EnpUpdateFsFileInfo(
    _Inout_ PEN_FILEINFO FileInfo,
    _In_opt_ PBK_VSS_OBJECT Vss
    );

VOID EnpSetDiffFlagsFileInfo(
    _Inout_ PEN_FILEINFO FileInfo,
    _In_ UCHAR DiffFlags
    );

// Misc.

NTSTATUS EnpCreateTransaction(
    _Out_ PHANDLE TransactionHandle,
    _In_opt_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpCommitAndCloseTransaction(
    _In_ NTSTATUS CurrentStatus,
    _In_opt_ HANDLE TransactionHandle,
    _In_ BOOLEAN TrivialCommit,
    _In_opt_ PEN_MESSAGE_HANDLER MessageHandler
    );

BOOLEAN EnpStartVssObject(
    _In_ PBK_CONFIG Config,
    _In_ PEN_FILEINFO Root,
    _Out_ PBK_VSS_OBJECT *Vss,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

VOID EnpConfigureVssObject(
    _In_ PBK_VSS_OBJECT Vss,
    _In_ PEN_FILEINFO Root,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

BOOLEAN EnpMatchFileName(
    _In_ PBK_CONFIG Config,
    _In_ PPH_STRING FileName
    );

BOOLEAN EnpMatchFileSize(
    _In_ PBK_CONFIG Config,
    _In_ PPH_STRING FileName,
    _In_ PLARGE_INTEGER Size
    );

BOOLEAN EnpMatchFileSizeAgainstExpression(
    _In_ PPH_STRING String,
    _In_ PPH_STRING FileName,
    _In_ PLARGE_INTEGER Size
    );

ULONG64 EnpStringToSize(
    _In_ PPH_STRINGREF String
    );

PPH_STRING EnpMapFileName(
    _In_ PBK_CONFIG Config,
    _In_opt_ PPH_STRINGREF FileName,
    _In_opt_ PPH_STRING FileNameString
    );

PPH_STRING EnpAppendComponentToPath(
    _In_ PPH_STRINGREF FileName,
    _In_ PPH_STRINGREF Component
    );

NTSTATUS EnpQueryFullAttributesFileWin32(
    _In_ PWSTR FileName,
    _Out_ PFILE_NETWORK_OPEN_INFORMATION FileInformation
    );

NTSTATUS EnpRenameFileWin32(
    _In_opt_ HANDLE FileHandle,
    _In_opt_ PWSTR FileName,
    _In_ PWSTR NewFileName
    );

NTSTATUS EnpCopyFileWin32(
    _In_ PWSTR FileName,
    _In_ PWSTR NewFileName,
    _In_ ULONG NewFileAttributes,
    _In_ BOOLEAN OverwriteIfExists
    );

VOID EnpFormatRevisionId(
    _In_ ULONGLONG RevisionId,
    _Out_writes_(17) PWSTR String
    );

PPH_STRING EnpFormatPackageName(
    _In_ PBK_CONFIG Config,
    _In_ ULONGLONG RevisionId
    );

PPH_STRING EnpFormatTempDatabaseFileName(
    _In_ PBK_CONFIG Config,
    _In_ BOOLEAN SameDirectory
    );

NTSTATUS EnpCreateDatabase(
    _In_ PBK_CONFIG Config
    );

NTSTATUS EnpOpenDatabase(
    _In_ PBK_CONFIG Config,
    _In_ BOOLEAN ReadOnly,
    _Out_ PDB_DATABASE *Database
    );

VOID EnpDefaultMessageHandler(
    _In_ ULONG Level,
    _In_ _Assume_refs_(1) PPH_STRING Message
    );

#endif
