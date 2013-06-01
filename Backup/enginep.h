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
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in PDB_DATABASE Database,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpSyncTreeFirstRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in PDBF_FILE Directory,
    __inout PEN_FILEINFO Root,
    __in PPK_ACTION_LIST ActionList,
    __in_opt PBK_VSS_OBJECT Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpSyncFileFirstRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in PEN_FILEINFO FileInfo,
    __in PPK_ACTION_LIST ActionList,
    __in_opt PBK_VSS_OBJECT Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpBackupNewRevision(
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in PDB_DATABASE Database,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpTestBackupNewRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffTreeNewRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONGLONG NewRevisionId,
    __in PDBF_FILE HeadDirectory,
    __in_opt PDBF_FILE DiffDirectory,
    __inout PEN_FILEINFO Root,
    __in_opt PPK_ACTION_LIST ActionList,
    __inout PULONGLONG NumberOfChanges,
    __in_opt PBK_VSS_OBJECT Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffDirectoryNewRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONGLONG NewRevisionId,
    __in PDBF_FILE HeadDirectory,
    __in_opt PDBF_FILE DiffDirectory,
    __in PEN_FILEINFO FileInfo,
    __in_opt PPK_ACTION_LIST ActionList,
    __inout PULONGLONG NumberOfChanges,
    __in_opt PBK_VSS_OBJECT Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffAddFileNewRevision(
    __in PDB_DATABASE Database,
    __in ULONGLONG NewRevisionId,
    __in PDBF_FILE HeadDirectory,
    __in PDBF_FILE DiffDirectory,
    __in PDBF_FILE ThisDirectoryInHead,
    __in PEN_FILEINFO FileInfo,
    __in BOOLEAN CreateDiffFile,
    __in PPK_ACTION_LIST ActionList,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffDeleteFileNewRevision(
    __in PDB_DATABASE Database,
    __in ULONGLONG NewRevisionId,
    __in PDBF_FILE HeadDirectory,
    __in PDBF_FILE DiffDirectory,
    __in PDBF_FILE ThisDirectoryInHead,
    __in PEN_FILEINFO DirectoryFileInfo,
    __in PDB_FILE_DIRECTORY_INFORMATION Entry,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffModifyFileNewRevision(
    __in PDB_DATABASE Database,
    __in ULONGLONG NewRevisionId,
    __in PDBF_FILE HeadDirectory,
    __in PDBF_FILE DiffDirectory,
    __in PDBF_FILE ThisDirectoryInHead,
    __in PEN_FILEINFO FileInfo,
    __in PDB_FILE_DIRECTORY_INFORMATION Entry,
    __in PPK_ACTION_LIST ActionList,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

BOOLEAN EnpDirectoryEntryCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    );

ULONG EnpDirectoryEntryHashFunction(
    __in PVOID Entry
    );

PDB_FILE_DIRECTORY_INFORMATION EnpFindDirectoryEntry(
    __in PPH_HASHTABLE Hashtable,
    __in PPH_STRING Name
    );

HRESULT EnpBackupPackageCallback(
    __in PK_PACKAGE_CALLBACK_MESSAGE Message,
    __in_opt PPK_ACTION Action,
    __in PVOID Parameter,
    __in_opt PVOID Context
    );

HRESULT EnpOpenStreamForFile(
    __in PEN_FILEINFO FileInfo,
    __in_opt PBK_VSS_OBJECT Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

// Merge

NTSTATUS EnpRevertToRevision(
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in PDB_DATABASE Database,
    __in ULONGLONG TargetRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpMergeDirectoryToHead(
    __in PDB_DATABASE Database,
    __in PDBF_FILE DirectoryInHead,
    __in PDBF_FILE DirectoryInDiff,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpTrimToRevision(
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in PDB_DATABASE Database,
    __in ULONGLONG TargetFirstRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpAddMergeFileNamesFromDirectory(
    __in PDB_DATABASE Database,
    __in PPH_HASHTABLE RevisionEntries,
    __in PDBF_FILE Directory,
    __in_opt PPH_STRING DirectoryName
    );

NTSTATUS EnpMergePackages(
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in ULONGLONG OldFirstRevisionId,
    __in ULONGLONG NewFirstRevisionId,
    __in PPH_HASHTABLE RevisionEntries,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

HRESULT EnpMergePackageCallback(
    __in PK_PACKAGE_CALLBACK_MESSAGE Message,
    __in_opt PPK_ACTION Action,
    __in PVOID Parameter,
    __in_opt PVOID Context
    );

NTSTATUS EnpUpdateDatabaseAfterTrim(
    __in PDB_DATABASE Database,
    __in ULONGLONG OldFirstRevisionId,
    __in ULONGLONG NewFirstRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpUpdateDirectoryMinimumRevisionIds(
    __in PDB_DATABASE Database,
    __in PDBF_FILE Directory,
    __in ULONGLONG FirstRevisionId
    );

BOOLEAN EnpRevisionEntryCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    );

ULONG EnpRevisionEntryHashFunction(
    __in PVOID Entry
    );

// Restore

NTSTATUS EnpRestoreFromRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in_opt ULONGLONG RevisionId,
    __in PPH_STRINGREF RestoreToDirectory,
    __in_opt PPH_STRINGREF RestoreToName,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpRestoreSingleFileFromRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in ULONGLONG RevisionId,
    __in PPH_STRINGREF RestoreToDirectory,
    __in_opt PPH_STRINGREF RestoreToName,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpRestoreDirectoryFromHead(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in PPH_STRINGREF RestoreToDirectory,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpAddRestoreFileNamesFromDirectory(
    __in PDB_DATABASE Database,
    __in PPH_HASHTABLE RevisionEntries,
    __in PDBF_FILE Directory,
    __in PPH_STRING DirectoryName
    );

NTSTATUS EnpRestoreDirectoryFromRevision(
    __in PBK_CONFIG Config,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in ULONGLONG RevisionId,
    __in PPH_STRINGREF RestoreToDirectory,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpMergeToHeadUntilRevision(
    __in PDB_DATABASE Database,
    __in ULONGLONG TargetRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PDBF_FILE *HeadDirectory
    );

NTSTATUS EnpExtractFromPackage(
    __in PBK_CONFIG Config,
    __in ULONG Flags,
    __in ULONGLONG RevisionId,
    __in_opt PPH_STRINGREF BaseFileName,
    __in PPH_HASHTABLE FileNames,
    __in PPH_STRINGREF RestoreToDirectory,
    __in_opt PPH_STRINGREF RestoreToName,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

HRESULT EnpRestorePackageCallback(
    __in PK_PACKAGE_CALLBACK_MESSAGE Message,
    __in_opt PPK_ACTION Action,
    __in PVOID Parameter,
    __in_opt PVOID Context
    );

NTSTATUS EnpQueryFileRevisions(
    __in PDB_DATABASE Database,
    __in PPH_STRINGREF FileName,
    __in ULONGLONG FirstRevisionId,
    __in ULONGLONG LastRevisionId,
    __out PEN_FILE_REVISION_INFORMATION *Entries,
    __out PULONG NumberOfEntries,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

// Other

NTSTATUS EnpCompareRevisions(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONGLONG BaseRevisionId,
    __in_opt ULONGLONG TargetRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpDiffDirectoryCompareRevisions(
    __in_opt PDB_DATABASE BaseDatabase,
    __in PDB_DATABASE TargetDatabase,
    __in_opt PDBF_FILE BaseDirectory,
    __in PDBF_FILE TargetDirectory,
    __in PPH_STRING FileName,
    __inout PULONGLONG NumberOfChanges,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

// File names

PPH_HASHTABLE EnpCreateFileNameHashtable(
    VOID
    );

VOID EnpDestroyFileNameHashtable(
    __in PPH_HASHTABLE Hashtable
    );

VOID EnpAddToFileNameHashtable(
    __in PPH_HASHTABLE Hashtable,
    __in PPH_STRING FileName
    );

PPH_STRING EnpFindInFileNameHashtable(
    __in PPH_HASHTABLE Hashtable,
    __in PPH_STRINGREF FileName
    );

BOOLEAN EnpRemoveFromFileNameHashtable(
    __in PPH_HASHTABLE Hashtable,
    __in PPH_STRINGREF FileName
    );

BOOLEAN EnpFileNameCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    );

ULONG EnpFileNameHashFunction(
    __in PVOID Entry
    );

// File info

typedef struct _EN_POPULATE_FS_CONTEXT
{
    PBK_CONFIG Config;
    PEN_FILEINFO FileInfo;
} EN_POPULATE_FS_CONTEXT, *PEN_POPULATE_FS_CONTEXT;

PEN_FILEINFO EnpCreateFileInfo(
    __in_opt PEN_FILEINFO Parent,
    __in_opt PPH_STRINGREF Name,
    __in_opt PPH_STRINGREF SourceName
    );

BOOLEAN EnpFileInfoCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    );

ULONG EnpFileInfoHashFunction(
    __in PVOID Entry
    );

VOID EnpCreateHashtableFileInfo(
    __inout PEN_FILEINFO FileInfo
    );

VOID EnpDestroyFileInfo(
    __in PEN_FILEINFO FileInfo
    );

VOID EnpClearFileInfo(
    __in PEN_FILEINFO FileInfo
    );

VOID EnpFreeFileInfo(
    __in PEN_FILEINFO FileInfo
    );

PEN_FILEINFO EnpFindFileInfo(
    __in PEN_FILEINFO FileInfo,
    __in PPH_STRINGREF Name
    );

PEN_FILEINFO EnpCreateRootFileInfo(
    VOID
    );

VOID EnpMapBaseNamesFileInfo(
    __in PBK_CONFIG Config,
    __inout PEN_FILEINFO FileInfo
    );

VOID EnpAddSourceToRoot(
    __in PBK_CONFIG Config,
    __in PEN_FILEINFO Root,
    __in PPH_STRINGREF SourceFileName,
    __in BOOLEAN Directory
    );

VOID EnpPopulateRootFileInfo(
    __in PBK_CONFIG Config,
    __inout PEN_FILEINFO Root
    );

BOOLEAN NTAPI EnpEnumDirectoryFile(
    __in PFILE_DIRECTORY_INFORMATION Information,
    __in_opt PVOID Context
    );

NTSTATUS EnpPopulateFsFileInfo(
    __in PBK_CONFIG Config,
    __inout PEN_FILEINFO FileInfo,
    __in_opt PBK_VSS_OBJECT Vss
    );

NTSTATUS EnpUpdateFsFileInfo(
    __inout PEN_FILEINFO FileInfo,
    __in_opt PBK_VSS_OBJECT Vss
    );

VOID EnpSetDiffFlagsFileInfo(
    __inout PEN_FILEINFO FileInfo,
    __in UCHAR DiffFlags
    );

// Misc.

NTSTATUS EnpCreateTransaction(
    __out PHANDLE TransactionHandle,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnpCommitAndCloseTransaction(
    __in NTSTATUS CurrentStatus,
    __in_opt HANDLE TransactionHandle,
    __in BOOLEAN TrivialCommit,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
    );

BOOLEAN EnpStartVssObject(
    __in PBK_CONFIG Config,
    __in PEN_FILEINFO Root,
    __out PBK_VSS_OBJECT *Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

VOID EnpConfigureVssObject(
    __in PBK_VSS_OBJECT Vss,
    __in PEN_FILEINFO Root,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

BOOLEAN EnpMatchFileName(
    __in PBK_CONFIG Config,
    __in PPH_STRING FileName
    );

BOOLEAN EnpMatchFileSize(
    __in PBK_CONFIG Config,
    __in PPH_STRING FileName,
    __in PLARGE_INTEGER Size
    );

BOOLEAN EnpMatchFileSizeAgainstExpression(
    __in PPH_STRING String,
    __in PPH_STRING FileName,
    __in PLARGE_INTEGER Size
    );

ULONG64 EnpStringToSize(
    __in PPH_STRINGREF String
    );

PPH_STRING EnpMapFileName(
    __in PBK_CONFIG Config,
    __in_opt PPH_STRINGREF FileName,
    __in_opt PPH_STRING FileNameString
    );

PPH_STRING EnpAppendComponentToPath(
    __in PPH_STRINGREF FileName,
    __in PPH_STRINGREF Component
    );

NTSTATUS EnpQueryFullAttributesFileWin32(
    __in PWSTR FileName,
    __out PFILE_NETWORK_OPEN_INFORMATION FileInformation
    );

NTSTATUS EnpRenameFileWin32(
    __in_opt HANDLE FileHandle,
    __in_opt PWSTR FileName,
    __in PWSTR NewFileName
    );

NTSTATUS EnpCopyFileWin32(
    __in PWSTR FileName,
    __in PWSTR NewFileName,
    __in ULONG NewFileAttributes,
    __in BOOLEAN OverwriteIfExists
    );

VOID EnpFormatRevisionId(
    __in ULONGLONG RevisionId,
    __out_ecount(17) PWSTR String
    );

PPH_STRING EnpFormatPackageName(
    __in PBK_CONFIG Config,
    __in ULONGLONG RevisionId
    );

PPH_STRING EnpFormatTempDatabaseFileName(
    __in PBK_CONFIG Config,
    __in BOOLEAN SameDirectory
    );

NTSTATUS EnpCreateDatabase(
    __in PBK_CONFIG Config
    );

NTSTATUS EnpOpenDatabase(
    __in PBK_CONFIG Config,
    __in BOOLEAN ReadOnly,
    __out PDB_DATABASE *Database
    );

VOID EnpDefaultMessageHandler(
    __in ULONG Level,
    __in __assumeRefs(1) PPH_STRING Message
    );

#endif
