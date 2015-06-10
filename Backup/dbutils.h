#ifndef DBUTILS_H
#define DBUTILS_H

#include "db.h"

NTSTATUS DbUtTouchFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_opt_ PLARGE_INTEGER TimeStamp
    );

NTSTATUS DbUtCreateParentDirectories(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE RootDirectory,
    _In_ PPH_STRINGREF FileName
    );

NTSTATUS DbUtCopyFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_opt_ PDBF_FILE RootDirectory,
    _In_ PPH_STRINGREF FileName,
    _Out_opt_ PDBF_FILE *NewFile
    );

NTSTATUS DbUtCopyDirectoryContents(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE FromDirectory,
    _In_ PDBF_FILE ToDirectory
    );

NTSTATUS DbUtDeleteDirectoryContents(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE Directory
    );

#endif
