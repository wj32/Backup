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
    __in PDB_DATABASE Database,
    __out_opt PULONG FileRva
    );

BOOLEAN DbpSetNameFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in PPH_STRINGREF Name
    );

BOOLEAN DbpLinkFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE ParentFile,
    __in ULONG ParentFileRva,
    __in PDBF_FILE File,
    __in ULONG FileRva
    );

BOOLEAN DbpUnlinkFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE ParentFile,
    __in PDBF_FILE File,
    __in ULONG FileRva
    );

PDBF_FILE DbpFindFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE ParentFile,
    __in PPH_STRINGREF Name,
    __out_opt PULONG FileRva
    );

NTSTATUS DbpRenameFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in_opt PDBF_FILE RootDirectory,
    __in PPH_STRINGREF NewFileName
    );

NTSTATUS DbpCopyAttributesFile(
    __in PDBF_FILE SourceFile,
    __in PDBF_FILE DestinationFile
    );

NTSTATUS DbpCopyDirectory(
    __in PDB_DATABASE SourceDatabase,
    __in PDBF_FILE SourceDirectory,
    __in PDB_DATABASE DestinationDatabase,
    __in PDBF_FILE DestinationDirectory
    );

#endif
