#ifndef DB_H
#define DB_H

typedef struct _DBF_FILE *PDBF_FILE;
typedef struct _DB_DATABASE *PDB_DATABASE;

// Attributes
#define DB_FILE_ATTRIBUTE_DIRECTORY 0x1
#define DB_FILE_ATTRIBUTE_DELETE_TAG 0x2

NTSTATUS DbCreateDatabase(
    __in PWSTR FileName
    );

NTSTATUS DbOpenDatabase(
    __out PDB_DATABASE *Database,
    __in PWSTR FileName,
    __in BOOLEAN ReadOnly,
    __in ULONG ShareAccess
    );

VOID DbCloseDatabase(
    __in PDB_DATABASE Database
    );

VOID DbQueryRevisionIdsDatabase(
    __in PDB_DATABASE Database,
    __out_opt PULONGLONG RevisionId,
    __out_opt PULONGLONG FirstRevisionId
    );

VOID DbSetRevisionIdsDatabase(
    __in PDB_DATABASE Database,
    __in_opt PULONGLONG RevisionId,
    __in_opt PULONGLONG FirstRevisionId
    );

VOID DbCloseFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File
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
    __in PDB_DATABASE Database,
    __in PPH_STRINGREF FileName,
    __in_opt PDBF_FILE RootDirectory,
    __in ULONG Attributes,
    __in ULONG CreateDisposition,
    __in ULONG Options,
    __out_opt PULONG CreateStatus,
    __out PDBF_FILE *File
    );

NTSTATUS DbDeleteFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File
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
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in DB_FILE_INFORMATION_CLASS FileInformationClass,
    __out_bcount(FileInformationLength) PVOID FileInformation,
    __in ULONG FileInformationLength
    );

NTSTATUS DbSetInformationFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in DB_FILE_INFORMATION_CLASS FileInformationClass,
    __in_bcount_opt(FileInformationLength) PVOID FileInformation,
    __in ULONG FileInformationLength
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
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __out PDB_FILE_DIRECTORY_INFORMATION *Entries,
    __out PULONG NumberOfEntries
    );

VOID DbFreeQueryDirectoryFile(
    __in PDB_FILE_DIRECTORY_INFORMATION Entries,
    __in ULONG NumberOfEntries
    );

ULONG DbHashName(
    __in PWSTR String,
    __in SIZE_T Count
    );

#endif
