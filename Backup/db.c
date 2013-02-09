/*
 * Backup -
 *   database
 *
 * Copyright (C) 2011-2013 wj32
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
#include "db.h"
#include "dbp.h"

NTSTATUS DbCreateDatabase(
    __in PWSTR FileName
    )
{
    NTSTATUS status;
    PPH_FILE_POOL pool;
    PH_FILE_POOL_PARAMETERS parameters;
    PDBF_ROOT root;
    ULONG rootRva;
    ULONGLONG userContext;
    PDBF_FILE rootDirectory;
    ULONG rootDirectoryRva;
    LARGE_INTEGER systemTime;

    memset(&parameters, 0, sizeof(PH_FILE_POOL_PARAMETERS));
    parameters.SegmentShift = 17;
    parameters.MaximumInactiveViews = 128;

    status = PhCreateFilePool2(
        &pool,
        FileName,
        FALSE,
        FILE_SHARE_READ,
        FILE_CREATE,
        &parameters
        );

    if (!NT_SUCCESS(status))
        return status;

    root = PhAllocateFilePool(pool, sizeof(DBF_ROOT), &rootRva);

    if (!root)
    {
        status = STATUS_UNSUCCESSFUL;
        goto Fail;
    }

    userContext = rootRva;
    PhSetUserContextFilePool(pool, &userContext);

    memset(root, 0, sizeof(DBF_ROOT));
    root->Magic = DBF_DATABASE_MAGIC;
    root->Version = DBF_DATABASE_VERSION;
    root->NextDataFileId = 1;

    rootDirectory = PhAllocateFilePool(pool, sizeof(DBF_FILE), &rootDirectoryRva);

    if (!rootDirectory)
    {
        status = STATUS_UNSUCCESSFUL;
        goto Fail;
    }

    root->RootDirectoryRva = rootDirectoryRva;

    memset(rootDirectory, 0, sizeof(DBF_FILE));
    rootDirectory->Attributes = DB_FILE_ATTRIBUTE_DIRECTORY;
    PhQuerySystemTime(&systemTime);
    rootDirectory->TimeStamp = systemTime.QuadPart;

    PhDestroyFilePool(pool);

    return STATUS_SUCCESS;

Fail:
    PhDestroyFilePool(pool);
    PhDeleteFileWin32(FileName);

    return status;
}

NTSTATUS DbOpenDatabase(
    __out PDB_DATABASE *Database,
    __in PWSTR FileName,
    __in BOOLEAN ReadOnly,
    __in ULONG ShareAccess
    )
{
    NTSTATUS status;
    PPH_FILE_POOL pool;
    PH_FILE_POOL_PARAMETERS parameters;
    ULONGLONG userContext;
    PDBF_ROOT root;
    PDBF_FILE rootDirectory;
    PDB_DATABASE database;

    memset(&parameters, 0, sizeof(PH_FILE_POOL_PARAMETERS));
    parameters.MaximumInactiveViews = 128;

    status = PhCreateFilePool2(
        &pool,
        FileName,
        ReadOnly,
        ShareAccess,
        FILE_OPEN,
        &parameters
        );

    if (!NT_SUCCESS(status))
        return status;

    PhGetUserContextFilePool(pool, &userContext);
    root = PhReferenceFilePoolByRva(pool, (ULONG)userContext);

    if (!root || root->Magic != DBF_DATABASE_MAGIC || root->Version != DBF_DATABASE_VERSION)
        goto PreDatabaseError;

    rootDirectory = PhReferenceFilePoolByRva(pool, root->RootDirectoryRva);

    if (!rootDirectory)
        goto PreDatabaseError;

    database = PhAllocate(sizeof(DB_DATABASE));
    database->Pool = pool;
    database->Root = root;
    database->RootDirectory = rootDirectory;

    *Database = database;

    return STATUS_SUCCESS;

PreDatabaseError:
    PhDestroyFilePool(pool);
    return STATUS_UNSUCCESSFUL;
}

VOID DbCloseDatabase(
    __in PDB_DATABASE Database
    )
{
    PhDestroyFilePool(Database->Pool);
    PhFree(Database);
}

NTSTATUS DbCopyDatabase(
    __in PWSTR SourceFileName,
    __in PWSTR DestinationFileName
    )
{
    NTSTATUS status;
    PDB_DATABASE sourceDatabase;
    PDB_DATABASE destinationDatabase;

    if (!NT_SUCCESS(status = DbOpenDatabase(&sourceDatabase, SourceFileName, TRUE, FILE_SHARE_READ)))
        return status;

    if (!NT_SUCCESS(status = DbCreateDatabase(DestinationFileName)))
    {
        DbCloseDatabase(sourceDatabase);
        return status;
    }

    if (!NT_SUCCESS(status = DbOpenDatabase(&destinationDatabase, DestinationFileName, FALSE, 0)))
    {
        DbCloseDatabase(sourceDatabase);
        return status;
    }

    destinationDatabase->Root->RevisionId = sourceDatabase->Root->RevisionId;
    destinationDatabase->Root->FirstRevisionId = sourceDatabase->Root->FirstRevisionId;

    status = DbpCopyAttributesFile(sourceDatabase->RootDirectory, destinationDatabase->RootDirectory);

    if (NT_SUCCESS(status))
        status = DbpCopyDirectory(sourceDatabase, sourceDatabase->RootDirectory, destinationDatabase, destinationDatabase->RootDirectory);

    DbCloseDatabase(destinationDatabase);
    DbCloseDatabase(sourceDatabase);

    return status;
}

VOID DbQueryRevisionIdsDatabase(
    __in PDB_DATABASE Database,
    __out_opt PULONGLONG RevisionId,
    __out_opt PULONGLONG FirstRevisionId
    )
{
    if (RevisionId)
        *RevisionId = Database->Root->RevisionId;
    if (FirstRevisionId)
        *FirstRevisionId = Database->Root->FirstRevisionId;
}

VOID DbSetRevisionIdsDatabase(
    __in PDB_DATABASE Database,
    __in_opt PULONGLONG RevisionId,
    __in_opt PULONGLONG FirstRevisionId
    )
{
    if (RevisionId)
        Database->Root->RevisionId = *RevisionId;
    if (FirstRevisionId)
        Database->Root->FirstRevisionId = *FirstRevisionId;
}

VOID DbCloseFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File
    )
{
    PhDereferenceFilePool(Database->Pool, File);
}

NTSTATUS DbCreateFile(
    __in PDB_DATABASE Database,
    __in PPH_STRINGREF FileName,
    __in_opt PDBF_FILE RootDirectory,
    __in ULONG Attributes,
    __in ULONG CreateDisposition,
    __in ULONG Options,
    __out_opt PULONG CreateStatus,
    __out PDBF_FILE *File
    )
{
    PH_STRINGREF currentName;
    PH_STRINGREF remainingName;
    PDBF_FILE currentFile;
    ULONG currentFileRva;
    PDBF_FILE newFile;
    ULONG newFileRva;
    LARGE_INTEGER systemTime;

    if (RootDirectory)
    {
        currentFile = RootDirectory;
        currentFileRva = PhEncodeRvaFilePool(Database->Pool, RootDirectory);
    }
    else
    {
        currentFile = Database->RootDirectory;
        currentFileRva = Database->Root->RootDirectoryRva;
    }

    PhReferenceFilePoolByRva(Database->Pool, currentFileRva);

    remainingName = *FileName;

    // Remove trailing backslashes.
    while (remainingName.Length != 0 && remainingName.Buffer[remainingName.Length / sizeof(WCHAR) - 1] == '\\')
        remainingName.Length -= sizeof(WCHAR);

    while (remainingName.Buffer != 0)
    {
        PhSplitStringRefAtChar(&remainingName, '\\', &currentName, &remainingName);

        if (currentName.Length == 0)
            continue; // ignore zero-length components

        if (!(currentFile->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        {
            PhDereferenceFilePoolByRva(Database->Pool, currentFileRva);
            return STATUS_OBJECT_PATH_NOT_FOUND;
        }

        newFile = DbpFindFile(Database, currentFile, &currentName, &newFileRva);

        if (!newFile)
        {
            if (remainingName.Buffer != 0)
            {
                PhDereferenceFilePoolByRva(Database->Pool, currentFileRva);
                return STATUS_OBJECT_PATH_NOT_FOUND;
            }

            if (CreateDisposition == DB_FILE_OPEN)
            {
                PhDereferenceFilePoolByRva(Database->Pool, currentFileRva);
                return STATUS_OBJECT_NAME_NOT_FOUND;
            }

            // The file doesn't exist, so create a new file.

            newFile = DbpAllocateFile(Database, &newFileRva);

            if (!newFile)
            {
                PhDereferenceFilePoolByRva(Database->Pool, currentFileRva);
                return STATUS_UNSUCCESSFUL;
            }

            if (!DbpSetNameFile(Database, newFile, &currentName))
            {
                PhFreeFilePool(Database->Pool, newFile);
                PhDereferenceFilePoolByRva(Database->Pool, currentFileRva);
                return STATUS_UNSUCCESSFUL;
            }

            if (!DbpLinkFile(Database, currentFile, currentFileRva, newFile, newFileRva))
            {
                PhFreeFilePoolByRva(Database->Pool, newFile->Name.Rva);
                PhFreeFilePool(Database->Pool, newFile);
                PhDereferenceFilePoolByRva(Database->Pool, currentFileRva);
                return STATUS_UNSUCCESSFUL;
            }

            newFile->Attributes = Attributes;
            PhQuerySystemTime(&systemTime);
            newFile->TimeStamp = systemTime.QuadPart;

            if (CreateStatus)
                *CreateStatus = DB_FILE_CREATED;

            *File = newFile;

            return STATUS_SUCCESS;
        }

        PhDereferenceFilePoolByRva(Database->Pool, currentFileRva);
        currentFile = newFile;
        currentFileRva = newFileRva;
    }

    // No more components to process. The file exists.

    if (CreateDisposition == DB_FILE_CREATE)
    {
        PhDereferenceFilePoolByRva(Database->Pool, currentFileRva);
        return STATUS_OBJECT_NAME_COLLISION;
    }

    if ((Options & DB_FILE_DIRECTORY_FILE) && !(currentFile->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY) ||
        (Options & DB_FILE_NON_DIRECTORY_FILE) && (currentFile->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
    {
        PhDereferenceFilePoolByRva(Database->Pool, currentFileRva);

        if (Options & DB_FILE_DIRECTORY_FILE)
            return STATUS_NOT_A_DIRECTORY;
        else
            return STATUS_FILE_IS_A_DIRECTORY;
    }

    if (CreateStatus)
        *CreateStatus = DB_FILE_OPENED;

    *File = currentFile;

    return STATUS_SUCCESS;
}

NTSTATUS DbDeleteFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File
    )
{
    ULONG fileRva;
    ULONG parentFileRva;
    PDBF_FILE parentFile;

    if (File == Database->RootDirectory)
        return STATUS_CANNOT_DELETE;
    if ((File->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY) && File->u.Directory.NumberOfFiles != 0)
        return STATUS_DIRECTORY_NOT_EMPTY;

    fileRva = PhEncodeRvaFilePool(Database->Pool, File);

    if (fileRva == 0)
        return STATUS_UNSUCCESSFUL;

    parentFileRva = File->ParentRva;
    parentFile = PhReferenceFilePoolByRva(Database->Pool, parentFileRva);

    if (!parentFile)
        return STATUS_UNSUCCESSFUL;

    DbpUnlinkFile(Database, parentFile, File, fileRva);
    PhDereferenceFilePoolByRva(Database->Pool, parentFileRva);

    if (File->Name.Rva != 0)
        PhFreeFilePoolByRva(Database->Pool, File->Name.Rva);

    PhFreeFilePool(Database->Pool, File);

    return STATUS_SUCCESS;
}

NTSTATUS DbQueryInformationFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in DB_FILE_INFORMATION_CLASS FileInformationClass,
    __out_bcount(FileInformationLength) PVOID FileInformation,
    __in ULONG FileInformationLength
    )
{
    switch (FileInformationClass)
    {
    case DbFileBasicInformation:
        {
            DB_FILE_BASIC_INFORMATION basicInfo;

            if (FileInformationLength != sizeof(DB_FILE_BASIC_INFORMATION))
                return STATUS_INFO_LENGTH_MISMATCH;

            basicInfo.Attributes = File->Attributes;
            basicInfo.TimeStamp.QuadPart = File->TimeStamp;
            basicInfo.RevisionId = File->RevisionId;

            memcpy(FileInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION));
        }
        break;
    case DbFileStandardInformation:
        {
            DB_FILE_STANDARD_INFORMATION standardInfo;

            if (FileInformationLength != sizeof(DB_FILE_STANDARD_INFORMATION))
                return STATUS_INFO_LENGTH_MISMATCH;

            if (File->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                standardInfo.NumberOfFiles = File->u.Directory.NumberOfFiles;
            else
                standardInfo.NumberOfFiles = 0;

            memcpy(FileInformation, &standardInfo, sizeof(DB_FILE_STANDARD_INFORMATION));
        }
        break;
    case DbFileDataInformation:
        {
            DB_FILE_DATA_INFORMATION dataInfo;

            if (FileInformationLength != sizeof(DB_FILE_DATA_INFORMATION))
                return STATUS_INFO_LENGTH_MISMATCH;
            if (File->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                return STATUS_INVALID_PARAMETER;

            dataInfo.EndOfFile.QuadPart = File->u.File.EndOfFile;
            dataInfo.LastBackupTime.QuadPart = File->u.File.LastBackupTime;

            memcpy(FileInformation, &dataInfo, sizeof(DB_FILE_DATA_INFORMATION));
        }
        break;
    default:
        return STATUS_INVALID_INFO_CLASS;
    }

    return STATUS_SUCCESS;
}

NTSTATUS DbSetInformationFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in DB_FILE_INFORMATION_CLASS FileInformationClass,
    __in_bcount_opt(FileInformationLength) PVOID FileInformation,
    __in ULONG FileInformationLength
    )
{
    NTSTATUS status;

    status = STATUS_SUCCESS;

    switch (FileInformationClass)
    {
    case DbFileBasicInformation:
        {
            PDB_FILE_BASIC_INFORMATION basicInfo;
            ULONG changedAttributes;

            if (FileInformationLength != sizeof(DB_FILE_BASIC_INFORMATION))
                return STATUS_INFO_LENGTH_MISMATCH;

            basicInfo = FileInformation;
            changedAttributes = File->Attributes ^ basicInfo->Attributes;

            if (changedAttributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                return STATUS_INVALID_PARAMETER;

            File->Attributes = basicInfo->Attributes;
            File->TimeStamp = basicInfo->TimeStamp.QuadPart;
            File->RevisionId = basicInfo->RevisionId;
        }
        break;
    case DbFileRevisionIdInformation:
        {
            PDB_FILE_REVISION_ID_INFORMATION revisionIdInfo;

            if (FileInformationLength != sizeof(DB_FILE_REVISION_ID_INFORMATION))
                return STATUS_INFO_LENGTH_MISMATCH;

            revisionIdInfo = FileInformation;
            File->RevisionId = revisionIdInfo->RevisionId;
        }
        break;
    case DbFileDataInformation:
        {
            PDB_FILE_DATA_INFORMATION dataInfo;

            if (FileInformationLength != sizeof(DB_FILE_DATA_INFORMATION))
                return STATUS_INFO_LENGTH_MISMATCH;
            if (File->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                return STATUS_INVALID_PARAMETER;

            dataInfo = FileInformation;
            File->u.File.EndOfFile = dataInfo->EndOfFile.QuadPart;
            File->u.File.LastBackupTime = dataInfo->LastBackupTime.QuadPart;
        }
        break;
    case DbFileRenameInformation:
        {
            PDB_FILE_RENAME_INFORMATION renameInfo;

            renameInfo = FileInformation;
            status = DbpRenameFile(Database, File, renameInfo->RootDirectory, &renameInfo->FileName);
        }
        break;
    default:
        return STATUS_INVALID_INFO_CLASS;
    }

    return status;
}

NTSTATUS DbQueryDirectoryFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __out PDB_FILE_DIRECTORY_INFORMATION *Entries,
    __out PULONG NumberOfEntries
    )
{
    NTSTATUS status;
    ULONG numberOfFiles;
    PDB_FILE_DIRECTORY_INFORMATION directoryInfo;
    PDB_FILE_DIRECTORY_INFORMATION currentDirectoryInfo;
    ULONG index;
    ULONG i;
    ULONG fileRva;
    PDBF_FILE file;
    ULONG nextFileRva;
    PWSTR nameBuffer;

    if (!(File->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        return STATUS_INVALID_PARAMETER;

    numberOfFiles = File->u.Directory.NumberOfFiles;
    directoryInfo = PhAllocate(sizeof(DB_FILE_DIRECTORY_INFORMATION) * numberOfFiles);
    currentDirectoryInfo = directoryInfo;
    index = 0;

    for (i = 0; i < DBF_NUMBER_OF_BUCKETS; i++)
    {
        fileRva = File->Buckets[i];

        while (fileRva != 0)
        {
            if (index >= numberOfFiles)
            {
                status = STATUS_UNSUCCESSFUL;
                goto Fail;
            }

            file = PhReferenceFilePoolByRva(Database->Pool, fileRva);

            if (!file)
            {
                status = STATUS_UNSUCCESSFUL;
                goto Fail;
            }

            nameBuffer = PhReferenceFilePoolByRva(Database->Pool, file->Name.Rva);

            if (!nameBuffer)
            {
                PhDereferenceFilePoolByRva(Database->Pool, fileRva);
                status = STATUS_UNSUCCESSFUL;
                goto Fail;
            }

            currentDirectoryInfo->Attributes = file->Attributes;
            currentDirectoryInfo->TimeStamp.QuadPart = file->TimeStamp;
            currentDirectoryInfo->RevisionId = file->RevisionId;
            currentDirectoryInfo->FileName = PhCreateStringEx(nameBuffer, file->Name.Length);

            if (!(file->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
            {
                currentDirectoryInfo->EndOfFile.QuadPart = file->u.File.EndOfFile;
                currentDirectoryInfo->LastBackupTime.QuadPart = file->u.File.LastBackupTime;
            }
            else
            {
                currentDirectoryInfo->EndOfFile.QuadPart = 0;
                currentDirectoryInfo->LastBackupTime.QuadPart = 0;
            }

            currentDirectoryInfo++;
            index++;

            PhDereferenceFilePoolByRva(Database->Pool, file->Name.Rva);

            nextFileRva = file->NextRva;
            PhDereferenceFilePoolByRva(Database->Pool, fileRva);
            fileRva = nextFileRva;
        }
    }

    *Entries = directoryInfo;
    *NumberOfEntries = numberOfFiles;

    return STATUS_SUCCESS;

Fail:
    for (i = 0; i < index; i++)
    {
        PhDereferenceObject(directoryInfo[i].FileName);
    }

    PhFree(directoryInfo);

    return status;
}

VOID DbFreeQueryDirectoryFile(
    __in PDB_FILE_DIRECTORY_INFORMATION Entries,
    __in ULONG NumberOfEntries
    )
{
    ULONG i;

    for (i = 0; i < NumberOfEntries; i++)
    {
        PhDereferenceObject(Entries[i].FileName);
    }

    PhFree(Entries);
}

ULONG DbHashName(
    __in PWSTR String,
    __in SIZE_T Count
    )
{
    ULONG hash = (ULONG)Count;

    if (Count == 0)
        return 0;

    do
    {
        hash = RtlUpcaseUnicodeChar(*String) + (hash << 6) + (hash << 16) - hash;
        String++;
    } while (--Count != 0);

    return hash;
}

PDBF_FILE DbpAllocateFile(
    __in PDB_DATABASE Database,
    __out_opt PULONG FileRva
    )
{
    PDBF_FILE file;

    file = PhAllocateFilePool(Database->Pool, sizeof(DBF_FILE), FileRva);

    if (file)
    {
        memset(file, 0, sizeof(DBF_FILE));
    }

    return file;
}

BOOLEAN DbpSetNameFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in PPH_STRINGREF Name
    )
{
    PWSTR nameBlock;
    ULONG nameBlockRva;

    nameBlock = PhAllocateFilePool(Database->Pool, (ULONG)Name->Length, &nameBlockRva);

    if (!nameBlock)
        return FALSE;

    memcpy(nameBlock, Name->Buffer, Name->Length);
    PhDereferenceFilePoolByRva(Database->Pool, nameBlockRva);

    if (File->Name.Rva != 0)
        PhFreeFilePoolByRva(Database->Pool, File->Name.Rva);

    File->Name.Length = (ULONG)Name->Length;
    File->Name.Rva = nameBlockRva;
    File->NameHash = DbHashName(Name->Buffer, Name->Length / sizeof(WCHAR));

    return TRUE;
}

BOOLEAN DbpLinkFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE ParentFile,
    __in ULONG ParentFileRva,
    __in PDBF_FILE File,
    __in ULONG FileRva
    )
{
    ULONG bucketIndex;

    if (File->ParentRva != 0)
        return FALSE;
    if (!(ParentFile->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;

    bucketIndex = DBF_HASH_TO_BUCKET(File->NameHash);

    File->NextRva = ParentFile->Buckets[bucketIndex];
    ParentFile->Buckets[bucketIndex] = FileRva;
    File->ParentRva = ParentFileRva;

    ParentFile->u.Directory.NumberOfFiles++;

    return TRUE;
}

BOOLEAN DbpUnlinkFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE ParentFile,
    __in PDBF_FILE File,
    __in ULONG FileRva
    )
{
    BOOLEAN result;
    ULONG bucketIndex;
    ULONG fileRva;
    PDBF_FILE file;
    ULONG previousFileRva;
    PDBF_FILE previousFile;

    if (File->ParentRva == 0)
        return FALSE;
    if (!(ParentFile->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;

    result = FALSE;
    bucketIndex = DBF_HASH_TO_BUCKET(File->NameHash);

    fileRva = ParentFile->Buckets[bucketIndex];
    previousFileRva = 0;

    if (fileRva == 0)
        return FALSE;

    do
    {
        file = PhReferenceFilePoolByRva(Database->Pool, fileRva);

        if (!file)
        {
            fileRva = 0;
            break;
        }

        if (fileRva == FileRva)
        {
            if (previousFileRva == 0)
            {
                ParentFile->Buckets[bucketIndex] = file->NextRva;
            }
            else
            {
                previousFile->NextRva = file->NextRva;
            }

            file->ParentRva = 0;
            ParentFile->u.Directory.NumberOfFiles--;

            result = TRUE;

            break;
        }

        if (previousFileRva != 0)
            PhDereferenceFilePoolByRva(Database->Pool, previousFileRva);

        previousFile = file;
        previousFileRva = fileRva;
        fileRva = file->NextRva;
    } while (fileRva != 0);

    if (fileRva != 0)
        PhDereferenceFilePoolByRva(Database->Pool, fileRva);
    if (previousFileRva != 0)
        PhDereferenceFilePoolByRva(Database->Pool, previousFileRva);

    return result;
}

PDBF_FILE DbpFindFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE ParentFile,
    __in PPH_STRINGREF Name,
    __out_opt PULONG FileRva
    )
{
    ULONG nameHash;
    ULONG bucketIndex;
    ULONG fileRva;
    PDBF_FILE file;
    ULONG nextFileRva;

    if (!(ParentFile->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        return NULL;

    nameHash = DbHashName(Name->Buffer, Name->Length / sizeof(WCHAR));
    bucketIndex = DBF_HASH_TO_BUCKET(nameHash);

    fileRva = ParentFile->Buckets[bucketIndex];

    while (fileRva != 0)
    {
        file = PhReferenceFilePoolByRva(Database->Pool, fileRva);

        if (!file)
            return NULL;

        if (file->NameHash == nameHash && file->Name.Length == Name->Length)
        {
            PWSTR nameBuffer;
            PH_STRINGREF nameSr;

            nameBuffer = PhReferenceFilePoolByRva(Database->Pool, file->Name.Rva);

            if (nameBuffer)
            {
                nameSr.Buffer = nameBuffer;
                nameSr.Length = Name->Length;

                if (PhEqualStringRef(Name, &nameSr, TRUE))
                {
                    PhDereferenceFilePoolByRva(Database->Pool, file->Name.Rva);

                    if (FileRva)
                        *FileRva = fileRva;

                    return file;
                }

                PhDereferenceFilePoolByRva(Database->Pool, file->Name.Rva);
            }
        }

        nextFileRva = file->NextRva;
        PhDereferenceFilePoolByRva(Database->Pool, fileRva);
        fileRva = nextFileRva;
    }

    return NULL;
}

NTSTATUS DbpRenameFile(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in_opt PDBF_FILE RootDirectory,
    __in PPH_STRINGREF NewFileName
    )
{
    NTSTATUS status;
    PH_STRINGREF newParentFileName;
    PH_STRINGREF newName;
    PDBF_FILE newParentFile;
    ULONG newParentFileRva;
    ULONG fileRva;
    ULONG parentFileRva;
    PDBF_FILE parentFile;
    PDBF_FILE existingFile;
    ULONG existingFileRva;
    BOOLEAN success;

    if (!RootDirectory)
        RootDirectory = Database->RootDirectory;

    if (File == Database->RootDirectory)
        return STATUS_INVALID_PARAMETER;

    if (!PhSplitStringRefAtLastChar(NewFileName, '\\', &newParentFileName, &newName))
    {
        PhInitializeEmptyStringRef(&newParentFileName);
        newName = *NewFileName;
    }

    if (newName.Length == 0)
        return STATUS_OBJECT_NAME_INVALID;

    status = DbCreateFile(
        Database,
        &newParentFileName,
        RootDirectory,
        0,
        DB_FILE_OPEN,
        DB_FILE_DIRECTORY_FILE,
        NULL,
        &newParentFile
        );

    if (!NT_SUCCESS(status))
        return status;

    newParentFileRva = PhEncodeRvaFilePool(Database->Pool, newParentFile);
    fileRva = PhEncodeRvaFilePool(Database->Pool, File);

    parentFileRva = File->ParentRva;
    parentFile = PhReferenceFilePoolByRva(Database->Pool, parentFileRva);

    if (!parentFile)
    {
        PhDereferenceFilePoolByRva(Database->Pool, newParentFileRva);
        return STATUS_UNSUCCESSFUL;
    }

    existingFile = DbpFindFile(Database, newParentFile, &newName, &existingFileRva);

    if (existingFile)
    {
        if (existingFile != File)
        {
            PhDereferenceFilePoolByRva(Database->Pool, existingFileRva);
            PhDereferenceFilePoolByRva(Database->Pool, parentFileRva);
            PhDereferenceFilePoolByRva(Database->Pool, newParentFileRva);
            return STATUS_OBJECT_NAME_COLLISION;
        }
        else
        {
            PhDereferenceFilePoolByRva(Database->Pool, existingFileRva);
        }
    }

    success = FALSE;

    if (DbpUnlinkFile(Database, parentFile, File, fileRva))
    {
        if (DbpSetNameFile(Database, File, &newName))
        {
            if (DbpLinkFile(Database, newParentFile, newParentFileRva, File, fileRva))
            {
                success = TRUE;
            }
        }
    }

    PhDereferenceFilePoolByRva(Database->Pool, parentFileRva);
    PhDereferenceFilePoolByRva(Database->Pool, newParentFileRva);

    if (!success)
        return STATUS_UNSUCCESSFUL;

    return STATUS_SUCCESS;
}

NTSTATUS DbpCopyAttributesFile(
    __in PDBF_FILE SourceFile,
    __in PDBF_FILE DestinationFile
    )
{
    if ((SourceFile->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY) != (DestinationFile->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        return STATUS_INVALID_PARAMETER;

    DestinationFile->Attributes = SourceFile->Attributes;
    DestinationFile->TimeStamp = SourceFile->TimeStamp;
    DestinationFile->RevisionId = SourceFile->RevisionId;

    if (!(SourceFile->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
    {
        DestinationFile->u.File.EndOfFile = SourceFile->u.File.EndOfFile;
        DestinationFile->u.File.LastBackupTime = SourceFile->u.File.LastBackupTime;
    }

    return STATUS_SUCCESS;
}

NTSTATUS DbpCopyDirectory(
    __in PDB_DATABASE SourceDatabase,
    __in PDBF_FILE SourceDirectory,
    __in PDB_DATABASE DestinationDatabase,
    __in PDBF_FILE DestinationDirectory
    )
{
    NTSTATUS status;
    PDB_FILE_DIRECTORY_INFORMATION entries;
    ULONG numberOfEntries;
    ULONG i;
    PDBF_FILE sourceFile;
    PDBF_FILE destinationFile;

    status = DbQueryDirectoryFile(SourceDatabase, SourceDirectory, &entries, &numberOfEntries);

    if (!NT_SUCCESS(status))
        return status;

    // Create all files, then copy subdirectories.
    // This keeps child entries close together, improving performance.

    for (i = 0; i < numberOfEntries; i++)
    {
        status = DbCreateFile(SourceDatabase, &entries[i].FileName->sr, SourceDirectory, 0, DB_FILE_OPEN, 0, NULL, &sourceFile);

        if (!NT_SUCCESS(status))
            goto CleanupExit;

        status = DbCreateFile(DestinationDatabase, &entries[i].FileName->sr, DestinationDirectory, sourceFile->Attributes, DB_FILE_CREATE, 0, NULL, &destinationFile);

        if (!NT_SUCCESS(status))
        {
            DbCloseFile(SourceDatabase, sourceFile);
            goto CleanupExit;
        }

        status = DbpCopyAttributesFile(sourceFile, destinationFile);
        DbCloseFile(DestinationDatabase, destinationFile);
        DbCloseFile(SourceDatabase, sourceFile);

        if (!NT_SUCCESS(status))
            goto CleanupExit;
    }

    for (i = 0; i < numberOfEntries; i++)
    {
        if (!(entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
            continue;

        status = DbCreateFile(SourceDatabase, &entries[i].FileName->sr, SourceDirectory, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &sourceFile);

        if (!NT_SUCCESS(status))
            goto CleanupExit;

        status = DbCreateFile(DestinationDatabase, &entries[i].FileName->sr, DestinationDirectory, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &destinationFile);

        if (!NT_SUCCESS(status))
        {
            DbCloseFile(SourceDatabase, sourceFile);
            goto CleanupExit;
        }

        status = DbpCopyDirectory(SourceDatabase, sourceFile, DestinationDatabase, destinationFile);
        DbCloseFile(DestinationDatabase, destinationFile);
        DbCloseFile(SourceDatabase, sourceFile);

        if (!NT_SUCCESS(status))
            goto CleanupExit;
    }

CleanupExit:
    DbFreeQueryDirectoryFile(entries, numberOfEntries);

    return status;
}
