#ifndef DBUTILS_H
#define DBUTILS_H

#include "db.h"

NTSTATUS DbUtTouchFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in_opt PLARGE_INTEGER TimeStamp
    );

NTSTATUS DbUtCreateParentDirectories(
    __in PDB_DATABASE Database,
    __in PDBF_FILE RootDirectory,
    __in PPH_STRINGREF FileName
    );

NTSTATUS DbUtCopyFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in_opt PDBF_FILE RootDirectory,
    __in PPH_STRINGREF FileName,
    __out_opt PDBF_FILE *NewFile
    );

NTSTATUS DbUtCopyDirectoryContents(
    __in PDB_DATABASE Database,
    __in PDBF_FILE FromDirectory,
    __in PDBF_FILE ToDirectory
    );

NTSTATUS DbUtDeleteDirectoryContents(
    __in PDB_DATABASE Database,
    __in PDBF_FILE Directory
    );

#endif
