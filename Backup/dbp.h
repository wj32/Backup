#ifndef DBP_H
#define DBP_H

#include <filepool.h>

// File structures

#define DBF_DATABASE_MAGIC ('bDkB')
#define DBF_DATABASE_VERSION 1
#define DBF_NUMBER_OF_BUCKETS 16
#define DBF_FIRST_REVISION_ID 1

#define DBF_HASH_TO_BUCKET(Hash) ((Hash) & (DBF_NUMBER_OF_BUCKETS - 1))

typedef struct _DBF_ROOT
{
    ULONG Magic;
    ULONG Version;
    ULONG RootDirectoryRva;
    ULONG Reserved1;
    ULONGLONG NextDataFileId;
    ULONGLONG RevisionId;
    ULONGLONG FirstRevisionId; // oldest revision
    ULONG Reserved2[8];
} DBF_ROOT, *PDBF_ROOT;

typedef struct _DBF_STRING
{
    ULONG Length;
    ULONG Rva;
} DBF_STRING, *PDBF_STRING;

typedef struct _DBF_FILE_DATA
{
    ULONGLONG EndOfFile;
    ULONGLONG LastBackupTime; // UTC
} DBF_FILE_DATA, *PDBF_FILE_DATA;

typedef struct _DBF_DIRECTORY_DATA
{
    ULONG NumberOfFiles;
} DBF_DIRECTORY_DATA, *PDBF_DIRECTORY_DATA;

typedef struct _DBF_FILE
{
    ULONG NextRva; // RVA to next file in chain
    ULONG ParentRva; // RVA to parent file
    DBF_STRING Name;
    ULONG NameHash;
    ULONG Attributes;
    ULONGLONG TimeStamp;
    ULONGLONG RevisionId;

    union
    {
        DBF_FILE_DATA File;
        DBF_DIRECTORY_DATA Directory;
    } u;

    ULONG Buckets[DBF_NUMBER_OF_BUCKETS]; // RVAs to child file chains
} DBF_FILE, *PDBF_FILE;

// Runtime

typedef struct _DB_DATABASE
{
    PPH_FILE_POOL Pool;
    PDBF_ROOT Root;
    PDBF_FILE RootDirectory;
} DB_DATABASE, *PDB_DATABASE;

PDBF_FILE DbpAllocateFile(
    _In_ PDB_DATABASE Database,
    _Out_opt_ PULONG FileRva
    );

BOOLEAN DbpSetNameFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_ PPH_STRINGREF Name
    );

BOOLEAN DbpLinkFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE ParentFile,
    _In_ ULONG ParentFileRva,
    _In_ PDBF_FILE File,
    _In_ ULONG FileRva
    );

BOOLEAN DbpUnlinkFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE ParentFile,
    _In_ PDBF_FILE File,
    _In_ ULONG FileRva
    );

PDBF_FILE DbpFindFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE ParentFile,
    _In_ PPH_STRINGREF Name,
    _Out_opt_ PULONG FileRva
    );

NTSTATUS DbpRenameFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_opt_ PDBF_FILE RootDirectory,
    _In_ PPH_STRINGREF NewFileName
    );

NTSTATUS DbpCopyAttributesFile(
    _In_ PDBF_FILE SourceFile,
    _In_ PDBF_FILE DestinationFile
    );

NTSTATUS DbpCopyDirectory(
    _In_ PDB_DATABASE SourceDatabase,
    _In_ PDBF_FILE SourceDirectory,
    _In_ PDB_DATABASE DestinationDatabase,
    _In_ PDBF_FILE DestinationDirectory
    );

#endif
