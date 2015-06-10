/*
 * Backup -
 *   database utilities
 *
 * Copyright (C) 2011 wj32
 *
 * This file is part of Backup.
 *
 * Backup is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Backup is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Backup.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "backup.h"
#include "dbutils.h"

NTSTATUS DbUtTouchFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_opt_ PLARGE_INTEGER TimeStamp
    )
{
    NTSTATUS status;
    DB_FILE_BASIC_INFORMATION basicInfo;

    status = DbQueryInformationFile(Database, File, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION));

    if (!NT_SUCCESS(status))
        return status;

    if (TimeStamp)
        basicInfo.TimeStamp = *TimeStamp;
    else
        PhQuerySystemTime(&basicInfo.TimeStamp);

    status = DbSetInformationFile(Database, File, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION));

    return status;
}

NTSTATUS DbUtCreateParentDirectories(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE RootDirectory,
    _In_ PPH_STRINGREF FileName
    )
{
    NTSTATUS status;
    PH_STRINGREF remainingPath;
    PH_STRINGREF component;
    PDBF_FILE currentDirectory;
    PDBF_FILE newDirectory;

    remainingPath = *FileName;

    // Remove leading backslashes.
    while (remainingPath.Length != 0 && remainingPath.Buffer[0] == '\\')
    {
        remainingPath.Buffer++;
        remainingPath.Length -= sizeof(WCHAR);
    }

    if (!PhSplitStringRefAtLastChar(&remainingPath, '\\', &remainingPath, &component))
        return STATUS_SUCCESS;

    currentDirectory = RootDirectory;

    while (remainingPath.Length != 0)
    {
        PhSplitStringRefAtChar(&remainingPath, '\\', &component, &remainingPath);

        if (component.Length != 0)
        {
            status = DbCreateFile(
                Database,
                &component,
                currentDirectory,
                DB_FILE_ATTRIBUTE_DIRECTORY,
                DB_FILE_OPEN_IF,
                DB_FILE_DIRECTORY_FILE,
                NULL,
                &newDirectory
                );

            if (!NT_SUCCESS(status))
                return status;

            if (currentDirectory != RootDirectory)
                DbCloseFile(Database, currentDirectory);

            currentDirectory = newDirectory;
        }
    }

    if (currentDirectory != RootDirectory)
        DbCloseFile(Database, currentDirectory);

    return STATUS_SUCCESS;
}

NTSTATUS DbUtCopyFile(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_opt_ PDBF_FILE RootDirectory,
    _In_ PPH_STRINGREF FileName,
    _Out_opt_ PDBF_FILE *NewFile
    )
{
    NTSTATUS status;
    PDBF_FILE newFile;
    DB_FILE_BASIC_INFORMATION basicInfo;
    DB_FILE_DATA_INFORMATION dataInfo;

    status = DbQueryInformationFile(Database, File, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION));

    if (!NT_SUCCESS(status))
        return status;

    if (!(basicInfo.Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
    {
        status = DbQueryInformationFile(Database, File, DbFileDataInformation, &dataInfo, sizeof(DB_FILE_DATA_INFORMATION));

        if (!NT_SUCCESS(status))
            return status;
    }

    status = DbCreateFile(Database, FileName, RootDirectory, basicInfo.Attributes, FILE_CREATE, 0, NULL, &newFile);

    if (!NT_SUCCESS(status))
        return status;

    DbSetInformationFile(Database, newFile, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION));

    if (!(basicInfo.Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        DbSetInformationFile(Database, newFile, DbFileDataInformation, &dataInfo, sizeof(DB_FILE_DATA_INFORMATION));

    if (NewFile)
        *NewFile = newFile;
    else
        DbCloseFile(Database, newFile);

    return STATUS_SUCCESS;
}

NTSTATUS DbUtCopyDirectoryContents(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE FromDirectory,
    _In_ PDBF_FILE ToDirectory
    )
{
    NTSTATUS status;
    PDB_FILE_DIRECTORY_INFORMATION entries;
    ULONG numberOfEntries;
    ULONG i;
    BOOLEAN someNotCopied;
    PDBF_FILE sourceFile;
    PDBF_FILE destinationFile;

    someNotCopied = FALSE;

    status = DbQueryDirectoryFile(Database, FromDirectory, &entries, &numberOfEntries);

    if (!NT_SUCCESS(status))
        return status;

    for (i = 0; i < numberOfEntries; i++)
    {
        status = DbCreateFile(Database, &entries[i].FileName->sr, FromDirectory, 0, DB_FILE_OPEN, 0, NULL, &sourceFile);

        if (!NT_SUCCESS(status))
        {
            someNotCopied = TRUE;
            continue;
        }

        status = DbUtCopyFile(Database, sourceFile, ToDirectory, &entries[i].FileName->sr, &destinationFile);

        if (!NT_SUCCESS(status))
        {
            DbCloseFile(Database, sourceFile);
            someNotCopied = TRUE;
            continue;
        }

        if (entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
        {
            status = DbUtCopyDirectoryContents(Database, sourceFile, destinationFile);

            if (!NT_SUCCESS(status) || status == STATUS_SOME_NOT_MAPPED)
                someNotCopied = TRUE;
        }

        DbCloseFile(Database, destinationFile);
        DbCloseFile(Database, sourceFile);
    }

    DbFreeQueryDirectoryFile(entries, numberOfEntries);

    if (someNotCopied)
        return STATUS_SOME_NOT_MAPPED;

    return STATUS_SUCCESS;
}

NTSTATUS DbUtDeleteDirectoryContents(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE Directory
    )
{
    NTSTATUS status;
    PDB_FILE_DIRECTORY_INFORMATION entries;
    ULONG numberOfEntries;
    ULONG i;
    BOOLEAN someNotDeleted;
    PDBF_FILE file;

    someNotDeleted = FALSE;

    status = DbQueryDirectoryFile(Database, Directory, &entries, &numberOfEntries);

    if (!NT_SUCCESS(status))
        return status;

    for (i = 0; i < numberOfEntries; i++)
    {
        status = DbCreateFile(Database, &entries[i].FileName->sr, Directory, 0, DB_FILE_OPEN, 0, NULL, &file);

        if (!NT_SUCCESS(status))
        {
            someNotDeleted = TRUE;
            continue;
        }

        if (entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
        {
            status = DbUtDeleteDirectoryContents(Database, file);

            if (!NT_SUCCESS(status) || status == STATUS_SOME_NOT_MAPPED)
                someNotDeleted = TRUE;
        }

        DbDeleteFile(Database, file);
    }

    DbFreeQueryDirectoryFile(entries, numberOfEntries);

    if (someNotDeleted)
        return STATUS_SOME_NOT_MAPPED;

    return STATUS_SUCCESS;
}
