#ifndef DB_H
#define DB_H

typedef struct _DBF_FILE *PDBF_FILE;
typedef struct _DB_DATABASE *PDB_DATABASE;

// Attributes
#define DB_FILE_ATTRIBUTE_DIRECTORY 0x1
#define DB_FILE_ATTRIBUTE_DELETE_TAG 0x2

NTSTATUS DbCreateDatabase(
    _In_ PWSTR FileName
    );

NTSTATUS DbOpenDatabase(
    _Out_ PDB_DATABASE *Database,
    _In_ PWSTR FileName,
    _In_ BOOLEAN ReadOnly,
    _In_ ULONG ShareAccess
    );

VOID DbCloseDatabase(
    _In_ PDB_DATABASE Database
    );

NTSTATUS DbCopyDatabase(
    _In_ PWSTR SourceFileName,
    _In_ PWSTR DestinationFileName
    );

VOID DbQueryRevisionIdsDatabase(
    _In_ PDB_DATABASE Database,
    _Out_opt_ PULONGLONG RevisionId,
    _Out_opt_ PULONGLONG FirstRevisionId
    );

VOID DbSetRevisionIdsDatabase(
    _In_ PDB_DATABASE Database,
    _In_opt_ PULONGLONG RevisionId,
    _In_opt_ PULONGLONG FirstRevisionId
    );

VOID DbCloseFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File
    );

// CreateDisposition
#define DB_FILE_OPEN 1
#define DB_FILE_CREATE 2
#define DB_FILE_OPEN_IF 3

// Options
#define DB_FILE_DIRECTORY_FILE 0x1
#define DB_FILE_NON_DIRECTORY_FILE 0x2

// CreateStatus
#define DB_FILE_OPENED 1
#define DB_FILE_CREATED 2

NTSTATUS DbCreateFile(
    _In_ PDB_DATABASE Database,
    _In_ PPH_STRINGREF FileName,
    _In_opt_ PDBF_FILE RootDirectory,
    _In_ ULONG Attributes,
    _In_ ULONG CreateDisposition,
    _In_ ULONG Options,
    _Out_opt_ PULONG CreateStatus,
    _Out_ PDBF_FILE *File
    );

NTSTATUS DbDeleteFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File
    );

typedef enum _DB_FILE_INFORMATION_CLASS
{
    DbFileBasicInformation, // qs
    DbFileStandardInformation, // q
    DbFileRevisionIdInformation, // s
    DbFileDataInformation, // qs
    DbFileRenameInformation // s
} DB_FILE_INFORMATION_CLASS, *PDB_FILE_INFORMATION_CLASS;

typedef struct _DB_FILE_BASIC_INFORMATION
{
    ULONG Attributes;
    LARGE_INTEGER TimeStamp;
    ULONGLONG RevisionId;
} DB_FILE_BASIC_INFORMATION, *PDB_FILE_BASIC_INFORMATION;

typedef struct _DB_FILE_STANDARD_INFORMATION
{
    ULONG NumberOfFiles;
} DB_FILE_STANDARD_INFORMATION, *PDB_FILE_STANDARD_INFORMATION;

typedef struct _DB_FILE_REVISION_ID_INFORMATION
{
    ULONGLONG RevisionId;
} DB_FILE_REVISION_ID_INFORMATION, *PDB_FILE_REVISION_ID_INFORMATION;

typedef struct _DB_FILE_DATA_INFORMATION
{
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER LastBackupTime;
} DB_FILE_DATA_INFORMATION, *PDB_FILE_DATA_INFORMATION;

typedef struct _DB_FILE_RENAME_INFORMATION
{
    PDBF_FILE RootDirectory;
    PH_STRINGREF FileName;
} DB_FILE_RENAME_INFORMATION, *PDB_FILE_RENAME_INFORMATION;

NTSTATUS DbQueryInformationFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_ DB_FILE_INFORMATION_CLASS FileInformationClass,
    _Out_writes_bytes_(FileInformationLength) PVOID FileInformation,
    _In_ ULONG FileInformationLength
    );

NTSTATUS DbSetInformationFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_ DB_FILE_INFORMATION_CLASS FileInformationClass,
    _In_reads_bytes_opt_(FileInformationLength) PVOID FileInformation,
    _In_ ULONG FileInformationLength
    );

typedef struct _DB_FILE_DIRECTORY_INFORMATION
{
    ULONG Attributes;
    LARGE_INTEGER TimeStamp;
    ULONGLONG RevisionId;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER LastBackupTime;
    PPH_STRING FileName;
} DB_FILE_DIRECTORY_INFORMATION, *PDB_FILE_DIRECTORY_INFORMATION;

NTSTATUS DbQueryDirectoryFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _Out_ PDB_FILE_DIRECTORY_INFORMATION *Entries,
    _Out_ PULONG NumberOfEntries
    );

VOID DbFreeQueryDirectoryFile(
    _In_ PDB_FILE_DIRECTORY_INFORMATION Entries,
    _In_ ULONG NumberOfEntries
    );

ULONG DbHashName(
    _In_ PWSTR String,
    _In_ SIZE_T Count
    );

#endif
