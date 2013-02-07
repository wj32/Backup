/*
 * Backup -
 *   backup engine
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

/*
 * Each backup location stores a set of revisions, starting at revision 1.
 * There are two types of data maintained: the database, and packages (archives).
 *
 * Database:
 * HEAD directory - \HEAD\...
 * diff directory - \0000000000000001\...
 * diff directory - \0000000000000002\...
 * ...
 *
 * The HEAD directory in the database always contains file and directory metadata
 * corresponding to the most recent revision. When multiple revisions are present,
 * each "diff" directory stores information needed to revert the HEAD directory
 * to an earlier revision. For example, if HEAD corresponds to revision 3, merging
 * 0000000000000002 with HEAD would produce the metadata for revision 2. To produce
 * revision 1, two merges must be performed: 0000000000000002, then 0000000000000001.
 * Having diffs in the reverse direction simplifies backup, restore and trim operations.
 *
 * Packages are stored in the forward direction due to the complexity of updating
 * existing archives. 0000000000000001.7z contains the files that were added in the
 * first revision, and each subsequent package contains the files that were added or
 * modified in that revision.
 *
 * Backup. A distinction is made between the first backup and subsequent backups.
 * To create the first revision, the file system structure is copied to the database
 * and the first package is created. In subsequent revisions, the current file system
 * structure is compared with the HEAD directory. The HEAD directory is updated to
 * reflect the new file system structure and each change is recorded in the diff
 * directory. A new package is created for new/modified files.
 *
 * Revert. To revert to an older revision, each diff directory up to the target
 * revision is merged with the HEAD directory. These diff directories and the
 * corresponding packages are then deleted.
 *
 * Trim. To delete old revisions, packages must be merged up to the target revision.
 * The old diff directories are then deleted. When merging packages, duplicate files
 * are avoided by scanning the database and creating an ignore list.
 *
 * Restore. Files and directories are restored by extracting files from the appropriate
 * packages.
 */

#include "backup.h"
#include "db.h"
#include "dbutils.h"
#include "package.h"
#include "vssobj.h"
#include "engine.h"
#include "enginep.h"
#include <shlobj.h>

PH_STRINGREF EnpBackslashString = PH_STRINGREF_INIT(L"\\");
PH_STRINGREF EnpNewSuffixString = PH_STRINGREF_INIT(L".new.tmp");

NTSTATUS EnQueryRevision(
    __in PBK_CONFIG Config,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PULONGLONG RevisionId,
    __out_opt PLARGE_INTEGER RevisionTimeStamp,
    __out_opt PULONGLONG FirstRevisionId,
    __out_opt PLARGE_INTEGER FirstRevisionTimeStamp
    )
{
    NTSTATUS status;
    PDB_DATABASE database;
    ULONGLONG revisionId;
    ULONGLONG firstRevisionId;
    PH_STRINGREF directoryName;
    WCHAR directoryNameBuffer[17];
    PDBF_FILE file;
    DB_FILE_BASIC_INFORMATION basicInfo;

    if (!MessageHandler)
        MessageHandler = EnpDefaultMessageHandler;

    status = EnpOpenDatabase(Config, FALSE, &database);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open database %s\\%s", Config->DestinationDirectory->Buffer, EN_DATABASE_NAME));
        return status;
    }

    DbQueryRevisionIdsDatabase(database, &revisionId, &firstRevisionId);

    if (RevisionId)
        *RevisionId = revisionId;
    if (FirstRevisionId)
        *FirstRevisionId = firstRevisionId;

    if (RevisionTimeStamp)
    {
        RevisionTimeStamp->QuadPart = 0;
        PhInitializeStringRef(&directoryName, L"head");

        if (NT_SUCCESS(DbCreateFile(database, &directoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &file)))
        {
            if (NT_SUCCESS(DbQueryInformationFile(database, file, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION))))
            {
                *RevisionTimeStamp = basicInfo.TimeStamp;
            }

            DbCloseFile(database, file);
        }
    }

    if (FirstRevisionTimeStamp)
    {
        FirstRevisionTimeStamp->QuadPart = 0;

        if (firstRevisionId != revisionId)
        {
            EnpFormatRevisionId(firstRevisionId, directoryNameBuffer);
            directoryName.Buffer = directoryNameBuffer;
            directoryName.Length = 16 * sizeof(WCHAR);
        }
        else
        {
            PhInitializeStringRef(&directoryName, L"head");
        }

        if (NT_SUCCESS(DbCreateFile(database, &directoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &file)))
        {
            if (NT_SUCCESS(DbQueryInformationFile(database, file, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION))))
            {
                *FirstRevisionTimeStamp = basicInfo.TimeStamp;
            }

            DbCloseFile(database, file);
        }
    }

    DbCloseDatabase(database);

    return status;
}

NTSTATUS EnBackupToRevision(
    __in PBK_CONFIG Config,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PULONGLONG RevisionId
    )
{
    NTSTATUS status;
    HANDLE transactionHandle;
    PDB_DATABASE database;
    ULONG privilege;
    PVOID privilegeState;
    ULONGLONG revisionId;

    if (!MessageHandler)
        MessageHandler = EnpDefaultMessageHandler;

    transactionHandle = NULL;

    if (Config->UseTransactions)
    {
        if (!NT_SUCCESS(status = EnpCreateTransaction(&transactionHandle, MessageHandler)))
            return status;

        RtlSetCurrentTransaction(transactionHandle);
    }
    else
    {
        RtlSetCurrentTransaction(NULL);
    }

    status = EnpOpenDatabase(Config, FALSE, &database);

    if (status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        MessageHandler(EN_MESSAGE_INFORMATION, PhFormatString(L"Creating database %s\\%s", Config->DestinationDirectory->Buffer, EN_DATABASE_NAME));
        status = EnpCreateDatabase(Config);

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to create database"));
            status = EnpCommitAndCloseTransaction(status, transactionHandle, TRUE, MessageHandler);
            return status;
        }

        status = EnpOpenDatabase(Config, FALSE, &database);
    }

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open database %s\\%s", Config->DestinationDirectory->Buffer, EN_DATABASE_NAME));
        status = EnpCommitAndCloseTransaction(status, transactionHandle, TRUE, MessageHandler);
        return status;
    }

    DbQueryRevisionIdsDatabase(database, &revisionId, NULL);

    privilege = SE_BACKUP_PRIVILEGE;
    privilegeState = NULL;
    RtlAcquirePrivilege(&privilege, 1, 0, &privilegeState);

    if (revisionId == 0)
    {
        status = EnpBackupFirstRevision(Config, transactionHandle, database, MessageHandler);

        if (NT_SUCCESS(status))
        {
            if (RevisionId)
                *RevisionId = 1;
        }
    }
    else
    {
        status = EnpBackupNewRevision(Config, transactionHandle, database, MessageHandler);

        if (NT_SUCCESS(status))
        {
            if (RevisionId)
                DbQueryRevisionIdsDatabase(database, RevisionId, NULL);
        }
    }

    if (privilegeState)
        RtlReleasePrivilege(privilegeState);

    DbCloseDatabase(database);
    status = EnpCommitAndCloseTransaction(status, transactionHandle, status == STATUS_ABANDONED, MessageHandler);

    return status;
}

NTSTATUS EnTestBackupToRevision(
    __in PBK_CONFIG Config,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PDB_DATABASE database;
    ULONG privilege;
    PVOID privilegeState;

    if (!MessageHandler)
        MessageHandler = EnpDefaultMessageHandler;

    status = EnpOpenDatabase(Config, TRUE, &database);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open database %s\\%s", Config->DestinationDirectory->Buffer, EN_DATABASE_NAME));
        return status;
    }

    privilege = SE_BACKUP_PRIVILEGE;
    privilegeState = NULL;
    RtlAcquirePrivilege(&privilege, 1, 0, &privilegeState);

    status = EnpTestBackupNewRevision(Config, database, MessageHandler);

    if (privilegeState)
        RtlReleasePrivilege(privilegeState);

    DbCloseDatabase(database);

    return status;
}

NTSTATUS EnRevertToRevision(
    __in PBK_CONFIG Config,
    __in ULONGLONG TargetRevisionId,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PULONGLONG RevisionId
    )
{
    NTSTATUS status;
    HANDLE transactionHandle;
    PDB_DATABASE database;

    if (!MessageHandler)
        MessageHandler = EnpDefaultMessageHandler;

    transactionHandle = NULL;

    if (Config->UseTransactions)
    {
        if (!NT_SUCCESS(status = EnpCreateTransaction(&transactionHandle, MessageHandler)))
            return status;

        RtlSetCurrentTransaction(transactionHandle);
    }
    else
    {
        RtlSetCurrentTransaction(NULL);
    }

    status = EnpOpenDatabase(Config, FALSE, &database);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open database %s\\%s", Config->DestinationDirectory->Buffer, EN_DATABASE_NAME));
        status = EnpCommitAndCloseTransaction(status, transactionHandle, TRUE, MessageHandler);
        return status;
    }

    status = EnpRevertToRevision(Config, transactionHandle, database, TargetRevisionId, MessageHandler);

    if (NT_SUCCESS(status))
    {
        if (RevisionId)
            DbQueryRevisionIdsDatabase(database, RevisionId, NULL);
    }

    DbCloseDatabase(database);
    status = EnpCommitAndCloseTransaction(status, transactionHandle, status == STATUS_ABANDONED, MessageHandler);

    return status;
}

NTSTATUS EnTrimToRevision(
    __in PBK_CONFIG Config,
    __in ULONGLONG TargetFirstRevisionId,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PULONGLONG FirstRevisionId
    )
{
    NTSTATUS status;
    HANDLE transactionHandle;
    PDB_DATABASE database;

    if (!MessageHandler)
        MessageHandler = EnpDefaultMessageHandler;

    transactionHandle = NULL;

    if (Config->UseTransactions)
    {
        if (!NT_SUCCESS(status = EnpCreateTransaction(&transactionHandle, MessageHandler)))
            return status;

        RtlSetCurrentTransaction(transactionHandle);
    }
    else
    {
        RtlSetCurrentTransaction(NULL);
    }

    status = EnpOpenDatabase(Config, FALSE, &database);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open database %s\\%s", Config->DestinationDirectory->Buffer, EN_DATABASE_NAME));
        status = EnpCommitAndCloseTransaction(status, transactionHandle, TRUE, MessageHandler);
        return status;
    }

    status = EnpTrimToRevision(Config, transactionHandle, database, TargetFirstRevisionId, MessageHandler);

    if (NT_SUCCESS(status))
    {
        if (FirstRevisionId)
            DbQueryRevisionIdsDatabase(database, NULL, FirstRevisionId);
    }

    DbCloseDatabase(database);
    status = EnpCommitAndCloseTransaction(status, transactionHandle, status == STATUS_ABANDONED, MessageHandler);

    return status;
}

NTSTATUS EnRestoreFromRevision(
    __in PBK_CONFIG Config,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in_opt ULONGLONG RevisionId,
    __in PPH_STRINGREF RestoreToDirectory,
    __in_opt PPH_STRINGREF RestoreToName,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PDB_DATABASE database;

    if (!MessageHandler)
        MessageHandler = EnpDefaultMessageHandler;

    status = EnpOpenDatabase(Config, TRUE, &database);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open database %s\\%s", Config->DestinationDirectory->Buffer, EN_DATABASE_NAME));
        return status;
    }

    status = EnpRestoreFromRevision(Config, database, Flags, FileName, RevisionId, RestoreToDirectory, RestoreToName, MessageHandler);
    DbCloseDatabase(database);

    return status;
}

NTSTATUS EnQueryFileRevisions(
    __in PBK_CONFIG Config,
    __in PPH_STRINGREF FileName,
    __in PEN_MESSAGE_HANDLER MessageHandler,
    __out PEN_FILE_REVISION_INFORMATION *Entries,
    __out PULONG NumberOfEntries
    )
{
    NTSTATUS status;
    PDB_DATABASE database;
    ULONGLONG revisionId;
    ULONGLONG firstRevisionId;

    if (!MessageHandler)
        MessageHandler = EnpDefaultMessageHandler;

    status = EnpOpenDatabase(Config, TRUE, &database);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open database %s\\%s", Config->DestinationDirectory->Buffer, EN_DATABASE_NAME));
        return status;
    }

    DbQueryRevisionIdsDatabase(database, &revisionId, &firstRevisionId);
    status = EnpQueryFileRevisions(database, FileName, firstRevisionId, revisionId, Entries, NumberOfEntries, MessageHandler);

    DbCloseDatabase(database);

    return status;
}

NTSTATUS EnCompareRevisions(
    __in PBK_CONFIG Config,
    __in ULONGLONG BaseRevisionId,
    __in_opt ULONGLONG TargetRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PDB_DATABASE database;

    if (!MessageHandler)
        MessageHandler = EnpDefaultMessageHandler;

    status = EnpOpenDatabase(Config, TRUE, &database);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open database %s\\%s", Config->DestinationDirectory->Buffer, EN_DATABASE_NAME));
        return status;
    }

    status = EnpCompareRevisions(Config, database, BaseRevisionId, TargetRevisionId, MessageHandler);
    DbCloseDatabase(database);

    return status;
}

NTSTATUS EnCompactDatabase(
    __in PBK_CONFIG Config,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    HANDLE transactionHandle;
    PH_STRINGREF name;
    PPH_STRING databaseFileName;
    PPH_STRING tempDatabaseFileName;
    PPH_STRING tempDatabaseFileName2;

    if (!MessageHandler)
        MessageHandler = EnpDefaultMessageHandler;

    transactionHandle = NULL;

    if (Config->UseTransactions)
    {
        if (!NT_SUCCESS(status = EnpCreateTransaction(&transactionHandle, MessageHandler)))
            return status;

        RtlSetCurrentTransaction(transactionHandle);
    }
    else
    {
        RtlSetCurrentTransaction(NULL);
    }

    PhInitializeStringRef(&name, EN_DATABASE_NAME);
    databaseFileName = EnpAppendComponentToPath(&Config->DestinationDirectory->sr, &name);
    tempDatabaseFileName = EnpFormatTempDatabaseFileName(Config, TRUE);
    tempDatabaseFileName2 = EnpFormatTempDatabaseFileName(Config, TRUE);

    status = DbCopyDatabase(databaseFileName->Buffer, tempDatabaseFileName->Buffer);

    if (!NT_SUCCESS(status))
    {
        PhDeleteFileWin32(tempDatabaseFileName->Buffer);
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to copy database %s to %s", databaseFileName->Buffer, tempDatabaseFileName->Buffer));
        goto CleanupExit;
    }

    if (NT_SUCCESS(status = EnpRenameFileWin32(NULL, databaseFileName->Buffer, tempDatabaseFileName2->Buffer)))
    {
        status = EnpRenameFileWin32(NULL, tempDatabaseFileName->Buffer, databaseFileName->Buffer);

        if (NT_SUCCESS(status))
            PhDeleteFileWin32(tempDatabaseFileName2->Buffer);
    }
    else
    {
        PhDeleteFileWin32(tempDatabaseFileName->Buffer);
    }

    if (!NT_SUCCESS(status))
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to rename database files"));

CleanupExit:
    status = EnpCommitAndCloseTransaction(status, transactionHandle, FALSE, MessageHandler);

    PhDereferenceObject(databaseFileName);
    PhDereferenceObject(tempDatabaseFileName);
    PhDereferenceObject(tempDatabaseFileName2);

    return status;
}

NTSTATUS EnpBackupFirstRevision(
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in PDB_DATABASE Database,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    HRESULT result;
    PDBF_FILE headDirectory;
    PH_STRINGREF headDirectoryName;
    PEN_FILEINFO rootInfo;
    PPK_ACTION_LIST actionList;
    PBK_VSS_OBJECT vss;
    PPH_STRING packageFileName;
    BOOLEAN fileStreamCreated;
    PPH_FILE_STREAM fileStream;
    PPK_FILE_STREAM pkFileStream;
    EN_PACKAGE_CALLBACK_CONTEXT updateContext;
    ULONGLONG revisionId;
    DB_FILE_REVISION_ID_INFORMATION revisionIdInfo;

    PhInitializeStringRef(&headDirectoryName, L"head");
    status = DbCreateFile(Database, &headDirectoryName, NULL, DB_FILE_ATTRIBUTE_DIRECTORY, DB_FILE_CREATE, 0, NULL, &headDirectory);

    if (status == STATUS_OBJECT_NAME_COLLISION)
        MessageHandler(EN_MESSAGE_WARNING, PhCreateString(L"HEAD directory already exists before first revision"));

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to create HEAD directory"));
        return status;
    }

    RtlSetCurrentTransaction(NULL);

    rootInfo = EnpCreateRootFileInfo();
    EnpPopulateRootFileInfo(Config, rootInfo);

    vss = NULL;

    if (Config->UseShadowCopy)
    {
        EnpStartVssObject(Config, rootInfo, &vss, MessageHandler);
    }

    actionList = PkCreateActionList();
    status = EnpSyncTreeFirstRevision(Config, Database, headDirectory, rootInfo, actionList, vss, MessageHandler);
    packageFileName = NULL;
    fileStreamCreated = FALSE;

    if (NT_SUCCESS(status))
    {
        packageFileName = EnpFormatPackageName(Config, 1);
        RtlSetCurrentTransaction(TransactionHandle);
        status = PhCreateFileStream(&fileStream, packageFileName->Buffer, FILE_GENERIC_READ | FILE_GENERIC_WRITE, 0, FILE_CREATE, 0);
        RtlSetCurrentTransaction(NULL);

        if (NT_SUCCESS(status))
        {
            fileStreamCreated = TRUE;
            pkFileStream = PkCreateFileStream(fileStream);
            PhDereferenceObject(fileStream);

            updateContext.Config = Config;
            updateContext.Database = Database;
            updateContext.Vss = vss;
            updateContext.MessageHandler = MessageHandler;
            result = PkCreatePackage(pkFileStream, actionList, EnpBackupPackageCallback, &updateContext);
            PkDereferenceFileStream(pkFileStream);

            if (!SUCCEEDED(result))
            {
                MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to update package %s: 0x%x", packageFileName->Buffer, result));
            }
        }
        else
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to create package %s", packageFileName->Buffer));
        }

        RtlSetCurrentTransaction(TransactionHandle);
    }

    PkDestroyActionList(actionList);

    if (vss)
        BkDestroyVssObject(vss);

    EnpDestroyFileInfo(rootInfo);

    if (!SUCCEEDED(result))
        status = STATUS_UNSUCCESSFUL;

    if (NT_SUCCESS(status))
    {
        revisionIdInfo.RevisionId = 1;
        DbSetInformationFile(Database, headDirectory, DbFileRevisionIdInformation, &revisionIdInfo, sizeof(DB_FILE_REVISION_ID_INFORMATION));

        revisionId = 1;
        DbSetRevisionIdsDatabase(Database, &revisionId, &revisionId);
    }
    else
    {
        if (fileStreamCreated)
            PhDeleteFileWin32(packageFileName->Buffer);
    }

    if (packageFileName)
        PhDereferenceObject(packageFileName);

    DbCloseFile(Database, headDirectory);

    return status;
}

NTSTATUS EnpSyncTreeFirstRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in PDBF_FILE Directory,
    __inout PEN_FILEINFO Root,
    __in PPK_ACTION_LIST ActionList,
    __in_opt PBK_VSS_OBJECT Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    SINGLE_LIST_ENTRY listHead; // file info stack
    PEN_FILEINFO info;
    PEN_FILEINFO *childInfoPtr;
    PEN_FILEINFO childInfo;

    listHead.Next = NULL;
    info = Root;
    info->DbFile = Directory;

    while (info)
    {
        switch (info->State)
        {
        case FileInfoPreEnum:
            if (info->FsExpand)
            {
                status = EnpPopulateFsFileInfo(Config, info, Vss);

                if (!NT_SUCCESS(status))
                    MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to list contents of %s", info->FullSourceFileName->Buffer));
            }

            if (info->Files)
            {
                PhBeginEnumHashtable(info->Files, &info->EnumContext);
                info->State = FileInfoEnum;
            }
            else
            {
                info->State = FileInfoPostEnum;
            }

            break;
        case FileInfoEnum:
            if (info->Files)
            {
                childInfoPtr = PhNextEnumHashtable(&info->EnumContext);

                if (!childInfoPtr)
                {
                    info->State = FileInfoPostEnum;
                    break;
                }

                childInfo = *childInfoPtr;

                status = EnpSyncFileFirstRevision(Config, Database, childInfo, ActionList, Vss, MessageHandler);

                if (NT_SUCCESS(status))
                {
                    if (childInfo->Directory)
                    {
                        // Process the child directory.
                        PushEntryList(&listHead, &info->ListEntry);
                        info = childInfo;
                        break;
                    }
                }
                else
                {
                    MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to sync %s", info->FullSourceFileName->Buffer));
                }
            }
            else
            {
                info->State = FileInfoPostEnum;
            }

            break;
        case FileInfoPostEnum:
            if (info != Root)
            {
                if (info->DbFile)
                    DbCloseFile(Database, info->DbFile);
            }

            // Go back to the parent directory.
            info = (PEN_FILEINFO)PopEntryList(&listHead);
            break;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS EnpSyncFileFirstRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in PEN_FILEINFO FileInfo,
    __in PPK_ACTION_LIST ActionList,
    __in_opt PBK_VSS_OBJECT Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PDBF_FILE file;
    ULONG actionFlags;
    DB_FILE_REVISION_ID_INFORMATION revisionIdInfo;
    DB_FILE_DATA_INFORMATION dataInfo;

    dprintf("Syncing %S from %S\n", FileInfo->FullFileName->Buffer, FileInfo->FullSourceFileName->Buffer);

    if (FileInfo->NeedFsInfo)
    {
        EnpUpdateFsFileInfo(FileInfo, Vss);
        FileInfo->NeedFsInfo = FALSE;
    }

    status = DbCreateFile(
        Database,
        &FileInfo->Name->sr,
        FileInfo->Parent->DbFile,
        FileInfo->Directory ? DB_FILE_ATTRIBUTE_DIRECTORY : 0,
        DB_FILE_CREATE,
        0,
        NULL,
        &file
        );

    if (!NT_SUCCESS(status))
        return status;

    actionFlags = 0;

    if (FileInfo->Directory)
        actionFlags |= PK_ACTION_DIRECTORY;

    PkAppendAddToActionList(ActionList, actionFlags, FileInfo->FullFileName, FileInfo);

    revisionIdInfo.RevisionId = 1;
    DbSetInformationFile(Database, file, DbFileRevisionIdInformation, &revisionIdInfo, sizeof(DB_FILE_REVISION_ID_INFORMATION));

    if (FileInfo->Directory)
    {
        FileInfo->DbFile = file;
    }
    else
    {
        dataInfo.EndOfFile = FileInfo->FileInformation.EndOfFile;
        dataInfo.LastBackupTime = FileInfo->FileInformation.LastWriteTime;
        DbSetInformationFile(Database, file, DbFileDataInformation, &dataInfo, sizeof(DB_FILE_DATA_INFORMATION));
        DbCloseFile(Database, file);
    }

    return STATUS_SUCCESS;
}

NTSTATUS EnpBackupNewRevision(
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in PDB_DATABASE Database,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    HRESULT result;
    ULONGLONG revisionId;
    PDBF_FILE headDirectory;
    PH_STRINGREF headDirectoryName;
    PDBF_FILE newHeadDirectory;
    PH_STRINGREF newHeadDirectoryName;
    ULONG createStatus;
    PDBF_FILE diffDirectory;
    WCHAR diffDirectoryNameBuffer[17];
    PH_STRINGREF diffDirectoryName;
    PEN_FILEINFO rootInfo;
    PPK_ACTION_LIST actionList;
    PBK_VSS_OBJECT vss;
    ULONGLONG numberOfChanges;
    PPH_STRING packageFileName;
    BOOLEAN fileStreamCreated;
    PPH_FILE_STREAM fileStream;
    PPK_FILE_STREAM pkFileStream;
    EN_PACKAGE_CALLBACK_CONTEXT updateContext;
    DB_FILE_RENAME_INFORMATION renameInfo;
    DB_FILE_REVISION_ID_INFORMATION revisionIdInfo;
    DB_FILE_BASIC_INFORMATION basicInfo;

    // Open the HEAD directory.

    PhInitializeStringRef(&headDirectoryName, L"head");
    status = DbCreateFile(Database, &headDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &headDirectory);

    if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        MessageHandler(EN_MESSAGE_WARNING, PhCreateString(L"HEAD directory does not exist"));

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to open HEAD directory"));
        return status;
    }

    // Create the NEWHEAD directory and copy the contents of HEAD to NEWHEAD.

    PhInitializeStringRef(&newHeadDirectoryName, L"newHead");
    status = DbCreateFile(Database, &newHeadDirectoryName, NULL, DB_FILE_ATTRIBUTE_DIRECTORY, DB_FILE_OPEN_IF, DB_FILE_DIRECTORY_FILE, &createStatus, &newHeadDirectory);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to create NEWHEAD directory"));
        DbCloseFile(Database, headDirectory);
        return status;
    }

    if (createStatus == DB_FILE_OPENED)
    {
        MessageHandler(EN_MESSAGE_WARNING, PhCreateString(L"NEWHEAD directory already exists; deleting contents"));
        DbUtDeleteDirectoryContents(Database, newHeadDirectory);
    }

    status = DbUtCopyDirectoryContents(Database, headDirectory, newHeadDirectory);

    if (!NT_SUCCESS(status) || status == STATUS_SOME_NOT_MAPPED)
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to copy HEAD to NEWHEAD"));
        DbUtDeleteDirectoryContents(Database, newHeadDirectory);
        DbDeleteFile(Database, newHeadDirectory);
        DbCloseFile(Database, headDirectory);

        if (status == STATUS_SOME_NOT_MAPPED)
            status = STATUS_UNSUCCESSFUL;

        return status;
    }

    // Create the diff directory.

    DbQueryRevisionIdsDatabase(Database, &revisionId, NULL);
    EnpFormatRevisionId(revisionId, diffDirectoryNameBuffer);
    diffDirectoryName.Buffer = diffDirectoryNameBuffer;
    diffDirectoryName.Length = 16 * sizeof(WCHAR);
    status = DbCreateFile(Database, &diffDirectoryName, NULL, DB_FILE_ATTRIBUTE_DIRECTORY, DB_FILE_OPEN_IF, DB_FILE_DIRECTORY_FILE, &createStatus, &diffDirectory);

    revisionId++;

    if (createStatus == DB_FILE_OPENED)
    {
        MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"%s directory already exists before revision %I64u; deleting contents", diffDirectoryNameBuffer, revisionId));
        DbUtDeleteDirectoryContents(Database, diffDirectory);
    }

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to create %s directory", diffDirectoryNameBuffer));
        DbUtDeleteDirectoryContents(Database, newHeadDirectory);
        DbDeleteFile(Database, newHeadDirectory);
        DbCloseFile(Database, headDirectory);
        return status;
    }

    // Perform the diff.

    RtlSetCurrentTransaction(NULL);

    rootInfo = EnpCreateRootFileInfo();
    EnpPopulateRootFileInfo(Config, rootInfo);

    vss = NULL;

    if (Config->UseShadowCopy)
    {
        EnpStartVssObject(Config, rootInfo, &vss, MessageHandler);
    }

    actionList = PkCreateActionList();
    numberOfChanges = 0;
    status = EnpDiffTreeNewRevision(Config, Database, revisionId, newHeadDirectory, diffDirectory, rootInfo, actionList, &numberOfChanges, vss, MessageHandler);
    result = S_OK;
    packageFileName = NULL;
    fileStreamCreated = FALSE;

    if (NT_SUCCESS(status) && PkQueryCountActionList(actionList) != 0)
    {
        packageFileName = EnpFormatPackageName(Config, revisionId);
        RtlSetCurrentTransaction(TransactionHandle);
        status = PhCreateFileStream(&fileStream, packageFileName->Buffer, FILE_GENERIC_READ | FILE_GENERIC_WRITE, 0, FILE_CREATE, 0);
        RtlSetCurrentTransaction(NULL);

        if (NT_SUCCESS(status))
        {
            fileStreamCreated = TRUE;
            pkFileStream = PkCreateFileStream(fileStream);
            PhDereferenceObject(fileStream);

            updateContext.Config = Config;
            updateContext.Database = Database;
            updateContext.Vss = vss;
            updateContext.MessageHandler = MessageHandler;
            result = PkCreatePackage(pkFileStream, actionList, EnpBackupPackageCallback, &updateContext);
            PkDereferenceFileStream(pkFileStream);

            if (!SUCCEEDED(result))
            {
                MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to update package %s: 0x%x", packageFileName->Buffer, result));
            }
        }
        else
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to create package %s", packageFileName->Buffer));
        }

        RtlSetCurrentTransaction(TransactionHandle);
    }

    PkDestroyActionList(actionList);

    if (vss)
        BkDestroyVssObject(vss);

    EnpDestroyFileInfo(rootInfo);

    if (!NT_SUCCESS(status) || !SUCCEEDED(result) || numberOfChanges == 0)
    {
        // Something went wrong or nothing changed.
        // Don't create a new revision and delete everything that we created so far.

        if (fileStreamCreated)
            PhDeleteFileWin32(packageFileName->Buffer);

        DbUtDeleteDirectoryContents(Database, diffDirectory);
        DbUtDeleteDirectoryContents(Database, newHeadDirectory);
        DbDeleteFile(Database, diffDirectory);
        DbDeleteFile(Database, newHeadDirectory);
        DbCloseFile(Database, headDirectory);

        if (packageFileName)
            PhDereferenceObject(packageFileName);

        if (NT_SUCCESS(status) && !SUCCEEDED(result))
            status = STATUS_UNSUCCESSFUL;

        if (NT_SUCCESS(status))
            status = STATUS_ABANDONED; // indicates that there are no changes

        return status;
    }

    if (packageFileName)
        PhDereferenceObject(packageFileName);

    revisionIdInfo.RevisionId = revisionId;
    DbSetInformationFile(Database, newHeadDirectory, DbFileRevisionIdInformation, &revisionIdInfo, sizeof(DB_FILE_REVISION_ID_INFORMATION));
    revisionIdInfo.RevisionId--;
    DbSetInformationFile(Database, diffDirectory, DbFileRevisionIdInformation, &revisionIdInfo, sizeof(DB_FILE_REVISION_ID_INFORMATION));

    if (NT_SUCCESS(DbQueryInformationFile(Database, headDirectory, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION))))
        DbUtTouchFile(Database, diffDirectory, &basicInfo.TimeStamp);

    DbCloseFile(Database, diffDirectory);

    // Delete the current HEAD directory and rename NEWHEAD to HEAD.
    DbUtDeleteDirectoryContents(Database, headDirectory);
    DbDeleteFile(Database, headDirectory);

    renameInfo.RootDirectory = NULL;
    PhInitializeStringRef(&renameInfo.FileName, L"head");
    status = DbSetInformationFile(Database, newHeadDirectory, DbFileRenameInformation, &renameInfo, sizeof(DB_FILE_RENAME_INFORMATION));

    if (!NT_SUCCESS(status))
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to rename NEWHEAD to HEAD; the database is in an unknown state"));

    if (!NT_SUCCESS(DbUtTouchFile(Database, newHeadDirectory, NULL)))
        MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to update timestamp on HEAD"));

    DbCloseFile(Database, newHeadDirectory);

    DbSetRevisionIdsDatabase(Database, &revisionId, NULL);

    return status;
}

NTSTATUS EnpTestBackupNewRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    ULONGLONG revisionId;
    PDBF_FILE headDirectory;
    PH_STRINGREF headDirectoryName;
    PEN_FILEINFO rootInfo;
    PBK_VSS_OBJECT vss;
    ULONGLONG numberOfChanges;

    // Open the HEAD directory.

    PhInitializeStringRef(&headDirectoryName, L"head");
    status = DbCreateFile(Database, &headDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &headDirectory);

    if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        MessageHandler(EN_MESSAGE_WARNING, PhCreateString(L"HEAD directory does not exist"));

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to open HEAD directory"));
        return status;
    }

    DbQueryRevisionIdsDatabase(Database, &revisionId, NULL);
    revisionId++;

    // Perform the diff.

    rootInfo = EnpCreateRootFileInfo();
    EnpPopulateRootFileInfo(Config, rootInfo);

    vss = NULL;

    if (Config->UseShadowCopy)
    {
        EnpStartVssObject(Config, rootInfo, &vss, MessageHandler);
    }

    numberOfChanges = 0;
    status = EnpDiffTreeNewRevision(Config, Database, revisionId, headDirectory, NULL, rootInfo, NULL, &numberOfChanges, vss, MessageHandler);

    if (numberOfChanges != 0)
        MessageHandler(EN_MESSAGE_INFORMATION, PhFormatString(L"%I64u change(s)", numberOfChanges));

    if (vss)
        BkDestroyVssObject(vss);

    EnpDestroyFileInfo(rootInfo);
    DbDeleteFile(Database, headDirectory);

    return status;
}

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
    )
{
    NTSTATUS status;
    SINGLE_LIST_ENTRY listHead; // file info stack
    PEN_FILEINFO info;
    PEN_FILEINFO *childInfoPtr;
    PEN_FILEINFO childInfo;

    listHead.Next = NULL;
    info = Root;

    while (info)
    {
        switch (info->State)
        {
        case FileInfoPreEnum:
            if (info->FsExpand)
            {
                status = EnpPopulateFsFileInfo(Config, info, Vss);

                if (!NT_SUCCESS(status))
                    MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to list contents of %s", info->FullSourceFileName->Buffer));
            }

            status = EnpDiffDirectoryNewRevision(Config, Database, NewRevisionId, HeadDirectory, DiffDirectory, info, ActionList, NumberOfChanges, Vss, MessageHandler);

            if (!NT_SUCCESS(status))
            {
                MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to diff %s", info->FullSourceFileName->Buffer));
            }

            if (info->Files)
            {
                PhBeginEnumHashtable(info->Files, &info->EnumContext);
                info->State = FileInfoEnum;
            }
            else
            {
                info->State = FileInfoPostEnum;
            }

            break;
        case FileInfoEnum:
            if (info->Files)
            {
                childInfoPtr = PhNextEnumHashtable(&info->EnumContext);

                if (!childInfoPtr)
                {
                    info->State = FileInfoPostEnum;
                    break;
                }

                childInfo = *childInfoPtr;

                if (childInfo->Directory)
                {
                    // Process the child directory.
                    PushEntryList(&listHead, &info->ListEntry);
                    info = childInfo;
                    break;
                }
            }
            else
            {
                info->State = FileInfoPostEnum;
            }

            break;
        case FileInfoPostEnum:
            if (info->DbFile)
                DbCloseFile(Database, info->DbFile);

            // Go back to the parent directory.
            info = (PEN_FILEINFO)PopEntryList(&listHead);
            break;
        }
    }

    return STATUS_SUCCESS;
}

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
    )
{
    NTSTATUS status;
    PDBF_FILE referenceDirectory;
    PDB_FILE_DIRECTORY_INFORMATION entries;
    ULONG numberOfEntries;
    PDB_FILE_DIRECTORY_INFORMATION entry;
    PPH_HASHTABLE directoryHashtable;
    ULONG i;
    PH_HASHTABLE_ENUM_CONTEXT enumContext;
    PEN_FILEINFO *childInfoPtr;
    PEN_FILEINFO childInfo;
    BOOLEAN fileInfoIsDirectory;
    BOOLEAN entryIsDirectory;
    BOOLEAN modified;

    dprintf("Performing diff for directory %S\n", FileInfo->FullFileName->Buffer);

    if (!FileInfo->Parent || FileInfo->Parent->DbFile)
    {
        status = DbCreateFile(
            Database,
            &FileInfo->Name->sr,
            FileInfo->Parent ? FileInfo->Parent->DbFile : HeadDirectory,
            0,
            DB_FILE_OPEN,
            DB_FILE_DIRECTORY_FILE,
            NULL,
            &referenceDirectory
            );
    }
    else
    {
        // For test mode.
        status = STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (DiffDirectory || (status != STATUS_OBJECT_NAME_NOT_FOUND && status != STATUS_NOT_A_DIRECTORY))
    {
        if (!NT_SUCCESS(status))
            return status;

        FileInfo->DbFile = referenceDirectory;

        status = DbQueryDirectoryFile(Database, referenceDirectory, &entries, &numberOfEntries);

        if (!NT_SUCCESS(status))
            return status;
    }
    else
    {
        // We're in test mode and we can't find the directory in HEAD (or we found a non-directory file).
        // This means a directory was added/switched in the file system. Pretend
        // that this DB directory exists but doesn't have anything in it.
        entries = NULL;
        numberOfEntries = 0;
        status = STATUS_SUCCESS;
    }

    // Build a hashtable from the directory entries.

    directoryHashtable = PhCreateHashtable(
        sizeof(PDB_FILE_DIRECTORY_INFORMATION),
        EnpDirectoryEntryCompareFunction,
        EnpDirectoryEntryHashFunction,
        numberOfEntries
        );
    entry = entries;

    for (i = 0; i < numberOfEntries; i++)
    {
        PhAddEntryHashtable(directoryHashtable, &entry);
        entry++;
    }

    // Detect files/directories that have been deleted.

    entry = entries;

    for (i = 0; i < numberOfEntries; i++)
    {
        if (!EnpFindFileInfo(FileInfo, &entry->FileName->sr))
        {
            // Deleted file
            (*NumberOfChanges)++;

            if (DiffDirectory)
                status = EnpDiffDeleteFileNewRevision(Database, NewRevisionId, HeadDirectory, DiffDirectory, referenceDirectory, FileInfo, entry, MessageHandler);

            if (NT_SUCCESS(status))
                MessageHandler(EN_MESSAGE_INFORMATION, PhFormatString(L"- %s\\%s", FileInfo->FullFileName->Buffer, entry->FileName->Buffer));
            else
                MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to process file delete for %s\\%s: 0x%x", FileInfo->FullFileName->Buffer, entry->FileName->Buffer, status));
        }

        entry++;
    }

    // Detect files/directories that have been added or modified.

    if (FileInfo->Files)
        PhBeginEnumHashtable(FileInfo->Files, &enumContext);
    else
        memset(&enumContext, 0, sizeof(PH_HASHTABLE_ENUM_CONTEXT)); // no files

    while (childInfoPtr = PhNextEnumHashtable(&enumContext))
    {
        childInfo = *childInfoPtr;

        if (childInfo->NeedFsInfo)
        {
            EnpUpdateFsFileInfo(childInfo, Vss);
            childInfo->NeedFsInfo = FALSE;
        }

        entry = EnpFindDirectoryEntry(directoryHashtable, childInfo->Name);

        if (entry)
        {
            modified = FALSE;

            fileInfoIsDirectory = childInfo->Directory;
            entryIsDirectory = !!(entry->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY);

            if (fileInfoIsDirectory != entryIsDirectory)
                modified = TRUE;

            if (!modified && !childInfo->Directory)
            {
                if (childInfo->FileInformation.EndOfFile.QuadPart != entry->EndOfFile.QuadPart)
                    modified = TRUE;
                if (childInfo->FileInformation.LastWriteTime.QuadPart != entry->LastBackupTime.QuadPart)
                    modified = TRUE;
            }

            if (modified)
            {
                // Modified file
                // or switched file (file has become directory or directory has become file)
                (*NumberOfChanges)++;

                if (DiffDirectory)
                    status = EnpDiffModifyFileNewRevision(Database, NewRevisionId, HeadDirectory, DiffDirectory, referenceDirectory, childInfo, entry, ActionList, MessageHandler);

                if (NT_SUCCESS(status))
                    MessageHandler(EN_MESSAGE_INFORMATION, PhFormatString(L"%c %s", fileInfoIsDirectory != entryIsDirectory ? 's' : 'm', childInfo->FullFileName->Buffer));
                else
                    MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to process file modify for %s: 0x%x", childInfo->FullFileName->Buffer, status));
            }
        }
        else
        {
            // Added file
            (*NumberOfChanges)++;

            if (DiffDirectory)
                status = EnpDiffAddFileNewRevision(Database, NewRevisionId, HeadDirectory, DiffDirectory, referenceDirectory, childInfo, TRUE, ActionList, MessageHandler);

            if (NT_SUCCESS(status))
                MessageHandler(EN_MESSAGE_INFORMATION, PhFormatString(L"+ %s", childInfo->FullFileName->Buffer));
            else
                MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to process file add for %s: 0x%x", childInfo->FullFileName->Buffer, status));
        }
    }

    PhDereferenceObject(directoryHashtable);

    if (entries)
        DbFreeQueryDirectoryFile(entries, numberOfEntries);

    return STATUS_SUCCESS;
}

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
    )
{
    NTSTATUS status;
    ULONG attributes;
    PDBF_FILE file;
    ULONG actionFlags;
    DB_FILE_REVISION_ID_INFORMATION revisionIdInfo;
    DB_FILE_DATA_INFORMATION dataInfo;

    attributes = 0;

    if (FileInfo->Directory)
        attributes |= DB_FILE_ATTRIBUTE_DIRECTORY;

    // Update the HEAD directory.

    status = DbCreateFile(
        Database,
        &FileInfo->Name->sr,
        ThisDirectoryInHead,
        attributes,
        DB_FILE_CREATE,
        0,
        NULL,
        &file
        );

    if (!NT_SUCCESS(status))
        return status;

    revisionIdInfo.RevisionId = NewRevisionId;
    DbSetInformationFile(Database, file, DbFileRevisionIdInformation, &revisionIdInfo, sizeof(DB_FILE_REVISION_ID_INFORMATION));

    if (!FileInfo->Directory)
    {
        dataInfo.EndOfFile = FileInfo->FileInformation.EndOfFile;
        dataInfo.LastBackupTime = FileInfo->FileInformation.LastWriteTime;
        DbSetInformationFile(Database, file, DbFileDataInformation, &dataInfo, sizeof(DB_FILE_DATA_INFORMATION));
    }

    DbCloseFile(Database, file);

    // Add the file to the package action list.

    actionFlags = 0;

    if (FileInfo->Directory)
        actionFlags |= PK_ACTION_DIRECTORY;

    PkAppendAddToActionList(ActionList, actionFlags, FileInfo->FullFileName, FileInfo);

    // Record a delete action in the diff directory.
    // We don't do this if the caller is handling a file modify.
    // We also don't do this if one of the directories in our hierarchy was switched from a file to a directory.

    if (CreateDiffFile && !(FileInfo->DiffFlags & EN_DIFF_SWITCHED))
    {
        DbUtCreateParentDirectories(Database, DiffDirectory, &FileInfo->FullFileName->sr);
        status = DbCreateFile(
            Database,
            &FileInfo->FullFileName->sr,
            DiffDirectory,
            attributes | DB_FILE_ATTRIBUTE_DELETE_TAG,
            DB_FILE_CREATE,
            0,
            NULL,
            &file
            );

        if (!NT_SUCCESS(status))
            return status;

        DbCloseFile(Database, file);
    }

    return STATUS_SUCCESS;
}

NTSTATUS EnpDiffDeleteFileNewRevision(
    __in PDB_DATABASE Database,
    __in ULONGLONG NewRevisionId,
    __in PDBF_FILE HeadDirectory,
    __in PDBF_FILE DiffDirectory,
    __in PDBF_FILE ThisDirectoryInHead,
    __in PEN_FILEINFO DirectoryFileInfo,
    __in PDB_FILE_DIRECTORY_INFORMATION Entry,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PPH_STRING fileName;
    PDBF_FILE file;
    DB_FILE_RENAME_INFORMATION renameInfo;

    // Move the file from the HEAD directory to the diff directory.

    status = DbCreateFile(
        Database,
        &Entry->FileName->sr,
        ThisDirectoryInHead,
        0,
        DB_FILE_OPEN,
        0,
        NULL,
        &file
        );

    if (!NT_SUCCESS(status))
        return status;

    if (DirectoryFileInfo->FullFileName->Length != 0)
    {
        fileName = EnpAppendComponentToPath(&DirectoryFileInfo->FullFileName->sr, &Entry->FileName->sr);
    }
    else
    {
        fileName = Entry->FileName;
        PhReferenceObject(fileName);
    }

    DbUtCreateParentDirectories(Database, DiffDirectory, &fileName->sr);

    renameInfo.RootDirectory = DiffDirectory;
    renameInfo.FileName = fileName->sr;
    status = DbSetInformationFile(Database, file, DbFileRenameInformation, &renameInfo, sizeof(DB_FILE_RENAME_INFORMATION));
    DbCloseFile(Database, file);
    PhDereferenceObject(fileName);

    return status;
}

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
    )
{
    NTSTATUS status;

    status = EnpDiffDeleteFileNewRevision(
        Database,
        NewRevisionId,
        HeadDirectory,
        DiffDirectory,
        ThisDirectoryInHead,
        FileInfo->Parent,
        Entry,
        MessageHandler
        );

    if (!NT_SUCCESS(status))
        return status;

    // Handle file -> directory switch
    if (!(Entry->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY) && FileInfo->Directory)
        EnpSetDiffFlagsFileInfo(FileInfo, EN_DIFF_SWITCHED);

    status = EnpDiffAddFileNewRevision(
        Database,
        NewRevisionId,
        HeadDirectory,
        DiffDirectory,
        ThisDirectoryInHead,
        FileInfo,
        FALSE,
        ActionList,
        MessageHandler
        );

    return status;
}

BOOLEAN EnpDirectoryEntryCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    )
{
    PDB_FILE_DIRECTORY_INFORMATION fileInfo1 = *(PDB_FILE_DIRECTORY_INFORMATION *)Entry1;
    PDB_FILE_DIRECTORY_INFORMATION fileInfo2 = *(PDB_FILE_DIRECTORY_INFORMATION *)Entry2;

    return PhEqualString(fileInfo1->FileName, fileInfo2->FileName, TRUE);
}

ULONG EnpDirectoryEntryHashFunction(
    __in PVOID Entry
    )
{
    PDB_FILE_DIRECTORY_INFORMATION fileInfo = *(PDB_FILE_DIRECTORY_INFORMATION *)Entry;

    return DbHashName(fileInfo->FileName->Buffer, fileInfo->FileName->Length / sizeof(WCHAR));
}

PDB_FILE_DIRECTORY_INFORMATION EnpFindDirectoryEntry(
    __in PPH_HASHTABLE Hashtable,
    __in PPH_STRING Name
    )
{
    DB_FILE_DIRECTORY_INFORMATION lookupEntry;
    PDB_FILE_DIRECTORY_INFORMATION lookupEntryPtr = &lookupEntry;
    PDB_FILE_DIRECTORY_INFORMATION *entry;

    lookupEntry.FileName = Name;
    entry = PhFindEntryHashtable(Hashtable, &lookupEntryPtr);

    if (entry)
        return *entry;
    else
        return NULL;
}

HRESULT EnpBackupPackageCallback(
    __in PK_PACKAGE_CALLBACK_MESSAGE Message,
    __in_opt PPK_ACTION Action,
    __in PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PEN_PACKAGE_CALLBACK_CONTEXT context = Context;
    PEN_FILEINFO fileInfo;

    if (Action)
        fileInfo = Action->Context;
    else
        fileInfo = NULL;

    switch (Message)
    {
    case PkGetSizeMessage:
    case PkGetAttributesMessage:
    case PkGetCreationTimeMessage:
    case PkGetAccessTimeMessage:
    case PkGetModifiedTimeMessage:
        *(PFILE_NETWORK_OPEN_INFORMATION)Parameter = fileInfo->FileInformation;

        if (fileInfo->Directory)
        {
            ((PFILE_NETWORK_OPEN_INFORMATION)Parameter)->FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        }

        break;
    case PkGetStreamMessage:
        {
            PPK_PARAMETER_GET_STREAM getStream = Parameter;

            if (!fileInfo->FileStreamAttempted)
            {
                EnpOpenStreamForFile(fileInfo, context->Vss, context->MessageHandler);
                fileInfo->FileStreamAttempted = TRUE;
            }

            getStream->FileStream = PkCreateFileStream(fileInfo->FileStream); // creates zero-length stream if NULL
            PhSwapReference(&fileInfo->FileStream, NULL);
        }
        break;
    case PkProgressMessage:
        {
            PPK_PARAMETER_PROGRESS progress = Parameter;
            PH_FORMAT format[3];

            if (progress->ProgressTotal != 0)
            {
                PhInitFormatS(&format[0], L"Compressing: ");
                PhInitFormatF(&format[1], (DOUBLE)progress->ProgressValue * 100 / progress->ProgressTotal, 2);
                format[1].Type |= FormatRightAlign;
                format[1].Width = 5;
                PhInitFormatC(&format[2], '%');

                context->MessageHandler(EN_MESSAGE_PROGRESS, PhFormat(format, 3, 0));
            }
        }
        break;
    }

    return S_OK;
}

NTSTATUS EnpOpenStreamForFile(
    __in PEN_FILEINFO FileInfo,
    __in_opt PBK_VSS_OBJECT Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PPH_STRING sourceFileName;
    HANDLE fileHandle;
    PPH_FILE_STREAM fileStream;

    if (Vss)
    {
        sourceFileName = BkMapFileNameVssObject(Vss, FileInfo->FullSourceFileName);
    }
    else
    {
        sourceFileName = FileInfo->FullSourceFileName;
        PhReferenceObject(sourceFileName);
    }

    status = PhCreateFileWin32(
        &fileHandle,
        sourceFileName->Buffer,
        FILE_GENERIC_READ,
        0,
        FILE_SHARE_READ,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT
        );

    if (!NT_SUCCESS(status))
    {
        status = PhCreateFileWin32(
            &fileHandle,
            sourceFileName->Buffer,
            FILE_GENERIC_READ,
            0,
            FILE_SHARE_READ,
            FILE_OPEN,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
            );
    }

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to open %s: 0x%x", sourceFileName->Buffer, status));
        PhDereferenceObject(sourceFileName);
        return status;
    }

    status = PhCreateFileStream2(&fileStream, fileHandle, 0, PAGE_SIZE);

    if (NT_SUCCESS(status))
    {
        FileInfo->FileStream = fileStream;
    }
    else
    {
        NtClose(fileHandle);
    }

    PhDereferenceObject(sourceFileName);

    return status;
}

NTSTATUS EnpRevertToRevision(
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in PDB_DATABASE Database,
    __in ULONGLONG TargetRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    ULONGLONG lastRevisionId;
    ULONGLONG firstRevisionId;
    PDBF_FILE headDirectory;
    PH_STRINGREF headDirectoryName;
    PDBF_FILE newHeadDirectory;
    PH_STRINGREF newHeadDirectoryName;
    ULONG createStatus;
    ULONGLONG revisionId;
    PDBF_FILE diffDirectory;
    WCHAR diffDirectoryNameBuffer[17];
    PH_STRINGREF diffDirectoryName;
    DB_FILE_BASIC_INFORMATION basicInfo;
    PPH_STRING packageFileName;
    DB_FILE_RENAME_INFORMATION renameInfo;
    DB_FILE_REVISION_ID_INFORMATION revisionIdInfo;

    DbQueryRevisionIdsDatabase(Database, &lastRevisionId, &firstRevisionId);

    if (TargetRevisionId < firstRevisionId || TargetRevisionId > lastRevisionId)
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Invalid revision ID '%I64u'", TargetRevisionId));
        return STATUS_INVALID_PARAMETER;
    }

    if (TargetRevisionId == lastRevisionId)
    {
        // Nothing to do
        return STATUS_ABANDONED;
    }

    // Open the HEAD directory.

    PhInitializeStringRef(&headDirectoryName, L"head");
    status = DbCreateFile(Database, &headDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &headDirectory);

    if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        MessageHandler(EN_MESSAGE_WARNING, PhCreateString(L"HEAD directory does not exist"));

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to open HEAD directory"));
        return status;
    }

    // Create the NEWHEAD directory and copy the contents of HEAD to NEWHEAD.

    PhInitializeStringRef(&newHeadDirectoryName, L"newHead");
    status = DbCreateFile(Database, &newHeadDirectoryName, NULL, DB_FILE_ATTRIBUTE_DIRECTORY, DB_FILE_OPEN_IF, DB_FILE_DIRECTORY_FILE, &createStatus, &newHeadDirectory);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to create or open NEWHEAD directory"));
        DbCloseFile(Database, headDirectory);
        return status;
    }

    if (createStatus == DB_FILE_OPENED)
    {
        MessageHandler(EN_MESSAGE_WARNING, PhCreateString(L"NEWHEAD directory already exists; deleting contents"));
        DbUtDeleteDirectoryContents(Database, newHeadDirectory);
    }

    status = DbUtCopyDirectoryContents(Database, headDirectory, newHeadDirectory);

    if (!NT_SUCCESS(status) || status == STATUS_SOME_NOT_MAPPED)
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to copy HEAD to NEWHEAD"));
        DbUtDeleteDirectoryContents(Database, newHeadDirectory);
        DbDeleteFile(Database, newHeadDirectory);
        DbCloseFile(Database, headDirectory);

        if (status == STATUS_SOME_NOT_MAPPED)
            status = STATUS_UNSUCCESSFUL;

        return status;
    }

    // Open the previous diff directories and merge them.

    for (revisionId = lastRevisionId - 1; revisionId >= TargetRevisionId; revisionId--)
    {
        EnpFormatRevisionId(revisionId, diffDirectoryNameBuffer);
        diffDirectoryName.Buffer = diffDirectoryNameBuffer;
        diffDirectoryName.Length = 16 * sizeof(WCHAR);
        status = DbCreateFile(Database, &diffDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &diffDirectory);

        if (NT_SUCCESS(status))
        {
            // Get the time stamp for this directory. We'll need it later.
            status = DbQueryInformationFile(Database, diffDirectory, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION));

            if (NT_SUCCESS(status))
            {
                MessageHandler(EN_MESSAGE_PROGRESS, PhFormatString(L"Merging %s to HEAD", diffDirectoryNameBuffer));
                status = EnpMergeDirectoryToHead(Database, newHeadDirectory, diffDirectory, MessageHandler);
                DbCloseFile(Database, diffDirectory);
            }
        }

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to merge %s", diffDirectoryNameBuffer));
            DbUtDeleteDirectoryContents(Database, newHeadDirectory);
            DbDeleteFile(Database, newHeadDirectory);
            DbCloseFile(Database, headDirectory);
            return status;
        }
    }

    // Delete the diff directories and package files.

    for (revisionId = lastRevisionId - 1; revisionId >= TargetRevisionId; revisionId--)
    {
        EnpFormatRevisionId(revisionId, diffDirectoryNameBuffer);
        diffDirectoryName.Buffer = diffDirectoryNameBuffer;
        diffDirectoryName.Length = 16 * sizeof(WCHAR);
        status = DbCreateFile(Database, &diffDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &diffDirectory);

        if (NT_SUCCESS(status))
        {
            DbUtDeleteDirectoryContents(Database, diffDirectory);
            DbDeleteFile(Database, diffDirectory);
        }
        else
        {
            MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to delete %s", diffDirectoryNameBuffer));
        }

        packageFileName = EnpFormatPackageName(Config, revisionId + 1);
        status = PhDeleteFileWin32(packageFileName->Buffer);

        // It's OK if the file didn't exist.
        if (!NT_SUCCESS(status) && status != STATUS_OBJECT_PATH_NOT_FOUND && status != STATUS_OBJECT_NAME_NOT_FOUND)
            MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to delete %s", packageFileName->Buffer));

        PhDereferenceObject(packageFileName);
    }

    revisionIdInfo.RevisionId = TargetRevisionId;
    DbSetInformationFile(Database, newHeadDirectory, DbFileRevisionIdInformation, &revisionIdInfo, sizeof(DB_FILE_REVISION_ID_INFORMATION));
    DbUtTouchFile(Database, newHeadDirectory, &basicInfo.TimeStamp);

    // Delete the current HEAD directory and rename NEWHEAD to HEAD.
    DbUtDeleteDirectoryContents(Database, headDirectory);
    DbDeleteFile(Database, headDirectory);

    renameInfo.RootDirectory = NULL;
    PhInitializeStringRef(&renameInfo.FileName, L"head");
    status = DbSetInformationFile(Database, newHeadDirectory, DbFileRenameInformation, &renameInfo, sizeof(DB_FILE_RENAME_INFORMATION));

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to rename NEWHEAD to HEAD; the database is in an unknown state"));
    }

    DbSetRevisionIdsDatabase(Database, &TargetRevisionId, NULL);

    return status;
}

NTSTATUS EnpMergeDirectoryToHead(
    __in PDB_DATABASE Database,
    __in PDBF_FILE DirectoryInHead,
    __in PDBF_FILE DirectoryInDiff,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PDB_FILE_DIRECTORY_INFORMATION entries;
    ULONG numberOfEntries;
    ULONG i;
    PDBF_FILE fileInHead;
    PDBF_FILE fileInDiff;
    DB_FILE_BASIC_INFORMATION basicInfo;
    BOOLEAN copyFromDiffToHead;

    status = DbQueryDirectoryFile(Database, DirectoryInDiff, &entries, &numberOfEntries);

    if (!NT_SUCCESS(status))
        return status;

    for (i = 0; i < numberOfEntries; i++)
    {
        copyFromDiffToHead = FALSE;
        status = DbCreateFile(Database, &entries[i].FileName->sr, DirectoryInHead, 0, DB_FILE_OPEN, 0, NULL, &fileInHead);

        if (NT_SUCCESS(status))
        {
            status = DbQueryInformationFile(Database, fileInHead, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION));

            if (NT_SUCCESS(status))
            {
                if (entries[i].Attributes & DB_FILE_ATTRIBUTE_DELETE_TAG)
                {
                    // File was added in the current revision; delete it.

                    if (basicInfo.Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                        DbUtDeleteDirectoryContents(Database, fileInHead);

                    status = DbDeleteFile(Database, fileInHead);

                    if (!NT_SUCCESS(status))
                        MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to delete '%s' during reversal of file add", entries[i].FileName->Buffer));
                }
                else
                {
                    if ((entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY) && (basicInfo.Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
                    {
                        // Both are directories, which doesn't indicate much.
                        // Scan the directory.

                        status = DbCreateFile(Database, &entries[i].FileName->sr, DirectoryInDiff, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &fileInDiff);

                        if (NT_SUCCESS(status))
                        {
                            status = EnpMergeDirectoryToHead(Database, fileInHead, fileInDiff, MessageHandler);
                            DbCloseFile(Database, fileInDiff);
                        }

                        DbCloseFile(Database, fileInHead);
                    }
                    else
                    {
                        // Both are files, indicating a modification, or there was a switch (file -> directory or directory -> file).
                        // Just delete the file in HEAD.

                        if (basicInfo.Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                            DbUtDeleteDirectoryContents(Database, fileInHead);

                        status = DbDeleteFile(Database, fileInHead);

                        if (NT_SUCCESS(status))
                            copyFromDiffToHead = TRUE;
                        else
                            MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to delete '%s' during reversal of file modify", entries[i].FileName->Buffer));
                    }
                }
            }
        }
        else if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            if (!(entries[i].Attributes & DB_FILE_ATTRIBUTE_DELETE_TAG))
            {
                // File was deleted in current revision; bring it back.
                copyFromDiffToHead = TRUE;
            }

            status = STATUS_SUCCESS;
        }

        if (copyFromDiffToHead)
        {
            status = DbCreateFile(Database, &entries[i].FileName->sr, DirectoryInDiff, 0, DB_FILE_OPEN, 0, NULL, &fileInDiff);

            if (NT_SUCCESS(status))
            {
                status = DbUtCopyFile(Database, fileInDiff, DirectoryInHead, &entries[i].FileName->sr, &fileInHead);

                if (NT_SUCCESS(status))
                {
                    if (entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                        status = DbUtCopyDirectoryContents(Database, fileInDiff, fileInHead);

                    DbCloseFile(Database, fileInHead);
                }

                DbCloseFile(Database, fileInDiff);
            }

            if (!NT_SUCCESS(status))
                MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to copy '%s'", entries[i].FileName->Buffer));
        }

        if (!NT_SUCCESS(status))
        {
            DbFreeQueryDirectoryFile(entries, numberOfEntries);
            return status;
        }
    }

    DbFreeQueryDirectoryFile(entries, numberOfEntries);

    return STATUS_SUCCESS;
}

NTSTATUS EnpTrimToRevision(
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in PDB_DATABASE Database,
    __in ULONGLONG TargetFirstRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    ULONGLONG lastRevisionId;
    ULONGLONG firstRevisionId;
    ULONGLONG revisionId;
    PPH_HASHTABLE revisionEntries;
    PDBF_FILE diffDirectory;
    WCHAR diffDirectoryNameBuffer[17];
    PH_STRINGREF diffDirectoryName;
    PH_HASHTABLE_ENUM_CONTEXT enumContext;
    PEN_REVISION_ENTRY revisionEntry;

    DbQueryRevisionIdsDatabase(Database, &lastRevisionId, &firstRevisionId);

    if (TargetFirstRevisionId < firstRevisionId || TargetFirstRevisionId > lastRevisionId)
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Invalid revision ID '%I64u'", TargetFirstRevisionId));
        return STATUS_INVALID_PARAMETER;
    }

    if (TargetFirstRevisionId == firstRevisionId)
    {
        // Nothing to do
        return STATUS_ABANDONED;
    }

    // Create a list of files to ignore from each revision.
    // In each revision, these files will be replaced by files in a later revision (or are eventually deleted).

    revisionEntries = PhCreateHashtable(
        sizeof(EN_REVISION_ENTRY),
        EnpRevisionEntryCompareFunction,
        EnpRevisionEntryHashFunction,
        10
        );

    for (revisionId = firstRevisionId; revisionId < TargetFirstRevisionId; revisionId++)
    {
        EnpFormatRevisionId(revisionId, diffDirectoryNameBuffer);
        diffDirectoryName.Buffer = diffDirectoryNameBuffer;
        diffDirectoryName.Length = 16 * sizeof(WCHAR);
        status = DbCreateFile(Database, &diffDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &diffDirectory);

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open %s directory", diffDirectoryNameBuffer));
            goto CleanupExit;
        }

        status = EnpAddMergeFileNamesFromDirectory(Database, revisionEntries, diffDirectory, NULL);
        DbCloseFile(Database, diffDirectory);

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to process %s directory", diffDirectoryNameBuffer));
            goto CleanupExit;
        }
    }

    // Merge packages up to the target revision.

    status = EnpMergePackages(Config, TransactionHandle, firstRevisionId, TargetFirstRevisionId, revisionEntries, MessageHandler);

    if (NT_SUCCESS(status))
    {
        status = EnpUpdateDatabaseAfterTrim(Database, firstRevisionId, TargetFirstRevisionId, MessageHandler);
    }

CleanupExit:
    PhBeginEnumHashtable(revisionEntries, &enumContext);

    while (revisionEntry = PhNextEnumHashtable(&enumContext))
    {
        EnpDestroyFileNameHashtable(revisionEntry->FileNames);
    }

    PhDereferenceObject(revisionEntries);

    return status;
}

NTSTATUS EnpAddMergeFileNamesFromDirectory(
    __in PDB_DATABASE Database,
    __in PPH_HASHTABLE RevisionEntries,
    __in PDBF_FILE Directory,
    __in_opt PPH_STRING DirectoryName
    )
{
    NTSTATUS status;
    PDB_FILE_DIRECTORY_INFORMATION entries;
    ULONG numberOfEntries;
    ULONG i;
    PPH_STRING fileName;
    PDBF_FILE file;
    PEN_REVISION_ENTRY revisionEntry;
    EN_REVISION_ENTRY localRevisionEntry;
    BOOLEAN added;

    status = DbQueryDirectoryFile(Database, Directory, &entries, &numberOfEntries);

    if (!NT_SUCCESS(status))
        return status;

    for (i = 0; i < numberOfEntries; i++)
    {
        if (DirectoryName)
        {
            fileName = EnpAppendComponentToPath(&DirectoryName->sr, &entries[i].FileName->sr);
        }
        else
        {
            fileName = entries[i].FileName;
            PhReferenceObject(fileName);
        }

        if (entries[i].RevisionId != 0 && !(entries[i].Attributes & DB_FILE_ATTRIBUTE_DELETE_TAG))
        {
            localRevisionEntry.RevisionId = entries[i].RevisionId;
            localRevisionEntry.FileNames = NULL;
            localRevisionEntry.DirectoryNames = NULL;
            revisionEntry = PhAddEntryHashtableEx(RevisionEntries, &localRevisionEntry, &added);

            if (added)
            {
                revisionEntry->FileNames = EnpCreateFileNameHashtable();
            }

            EnpAddToFileNameHashtable(revisionEntry->FileNames, fileName);
        }

        if (entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
        {
            status = DbCreateFile(Database, &entries[i].FileName->sr, Directory, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &file);

            if (NT_SUCCESS(status))
            {
                status = EnpAddMergeFileNamesFromDirectory(Database, RevisionEntries, file, fileName);
            }

            if (!NT_SUCCESS(status))
            {
                DbCloseFile(Database, file);
                PhDereferenceObject(fileName);
                break;
            }

            DbCloseFile(Database, file);
        }

        PhDereferenceObject(fileName);
    }

    DbFreeQueryDirectoryFile(entries, numberOfEntries);

    return status;
}

NTSTATUS EnpMergePackages(
    __in PBK_CONFIG Config,
    __in_opt HANDLE TransactionHandle,
    __in ULONGLONG OldFirstRevisionId,
    __in ULONGLONG NewFirstRevisionId,
    __in PPH_HASHTABLE RevisionEntries,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    HRESULT result;
    PPH_STRING targetPackageFileName;
    ULONGLONG revisionId;
    PPK_ACTION_LIST actionList;
    PPH_FILE_STREAM fileStream;
    PPH_STRING mergePackageFileName;
    PPK_FILE_STREAM pkMergePackageFileStream;
    PPK_PACKAGE mergePackage;
    PPH_STRING basePackageFileName;
    PPK_PACKAGE basePackage;
    EN_REVISION_ENTRY lookupRevisionEntry;
    PEN_REVISION_ENTRY revisionEntry;
    PPK_ACTION_LIST actionListToMerge;
    EN_PACKAGE_CALLBACK_CONTEXT updateContext;
    PPK_ACTION_SEGMENT segment;
    ULONG i;
    PPH_STRING newPackageFileName;
    PPK_FILE_STREAM pkNewPackageFileStream;

    targetPackageFileName = EnpFormatPackageName(Config, NewFirstRevisionId);
    basePackageFileName = NULL;
    basePackage = NULL;
    newPackageFileName = NULL;
    pkNewPackageFileStream = NULL;
    actionList = PkCreateActionList();
    RtlSetCurrentTransaction(NULL);

    updateContext.MessageHandler = MessageHandler;

    for (revisionId = OldFirstRevisionId; revisionId <= NewFirstRevisionId; revisionId++)
    {
        mergePackageFileName = EnpFormatPackageName(Config, revisionId);
        result = S_OK;

        if (RtlDoesFileExists_U(mergePackageFileName->Buffer))
        {
            status = PhCreateFileStream(
                &fileStream,
                mergePackageFileName->Buffer,
                FILE_GENERIC_READ,
                0,
                FILE_OPEN,
                FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
                );

            if (!NT_SUCCESS(status))
            {
                MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open %s", mergePackageFileName->Buffer));
                PhDereferenceObject(mergePackageFileName);
                goto CleanupExit;
            }

            pkMergePackageFileStream = PkCreateFileStream(fileStream);
            PhDereferenceObject(fileStream);
            MessageHandler(EN_MESSAGE_PROGRESS, PhFormatString(L"Processing %s", mergePackageFileName->Buffer));

            // Open the package.
            // If this isn't the target revision, filter out the files that will be replaced by the files in a later revision's package.

            updateContext.Merge.IgnoreFileNames = NULL;

            if (revisionId != NewFirstRevisionId)
            {
                lookupRevisionEntry.RevisionId = revisionId;
                revisionEntry = PhFindEntryHashtable(RevisionEntries, &lookupRevisionEntry);

                if (revisionEntry)
                    updateContext.Merge.IgnoreFileNames = revisionEntry->FileNames;
            }

            if (basePackage)
            {
                actionListToMerge = PkCreateActionList();
                result = PkOpenPackageWithFilter(
                    pkMergePackageFileStream,
                    actionListToMerge,
                    revisionId != NewFirstRevisionId ? EnpMergePackageCallback : NULL,
                    revisionId != NewFirstRevisionId ? &updateContext : NULL,
                    &mergePackage
                    );

                if (SUCCEEDED(result))
                {
                    segment = actionListToMerge->FirstSegment;

                    while (segment)
                    {
                        for (i = 0; i < segment->Count; i++)
                        {
                            PkAppendAddFromPackageToActionList(
                                actionList,
                                mergePackage,
                                segment->Actions[i].u.Update.Index,
                                NULL
                                );
                        }

                        segment = segment->Next;
                    }

                    PkDereferencePackage(mergePackage);
                }

                PkDestroyActionList(actionListToMerge);
            }
            else if (revisionId != NewFirstRevisionId)
            {
                // Use this as our base package.

                result = PkOpenPackageWithFilter(
                    pkMergePackageFileStream,
                    actionList,
                    EnpMergePackageCallback,
                    &updateContext,
                    &basePackage
                    );

                if (SUCCEEDED(result))
                {
                    basePackageFileName = mergePackageFileName;
                    mergePackageFileName = NULL;
                }
            }
            else
            {
                // We don't have a base package and we're already at the target revision.
                // Nothing needs to be done.
            }

            if (pkMergePackageFileStream)
                PkDereferenceFileStream(pkMergePackageFileStream);
        }

        if (!SUCCEEDED(result))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(
                L"Unable to process package %s: 0x%x",
                mergePackageFileName ? mergePackageFileName->Buffer : basePackageFileName->Buffer, result
                ));

            if (mergePackageFileName)
                PhDereferenceObject(mergePackageFileName);

            status = STATUS_UNSUCCESSFUL;
            goto CleanupExit;
        }

        if (mergePackageFileName)
            PhDereferenceObject(mergePackageFileName);
    }

    if (basePackage)
    {
        RtlSetCurrentTransaction(TransactionHandle);

        newPackageFileName = PhConcatStringRef2(&basePackageFileName->sr, &EnpNewSuffixString);
        status = PhCreateFileStream(
            &fileStream,
            newPackageFileName->Buffer,
            FILE_GENERIC_READ | FILE_GENERIC_WRITE,
            0,
            FILE_CREATE,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
            );

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to create %s", newPackageFileName->Buffer));
            goto CleanupExit;
        }

        pkNewPackageFileStream = PkCreateFileStream(fileStream);
        PhDereferenceObject(fileStream);

        MessageHandler(EN_MESSAGE_PROGRESS, PhCreateString(L"Merging packages"));
        result = PkUpdatePackage(
            pkNewPackageFileStream,
            basePackage,
            actionList,
            EnpMergePackageCallback,
            &updateContext
            );

        PkDereferenceFileStream(pkNewPackageFileStream);
        pkNewPackageFileStream = NULL;
        PkDereferencePackage(basePackage);
        basePackage = NULL;
        PkDestroyActionList(actionList);
        actionList = NULL;

        if (!SUCCEEDED(result))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to merge packages: 0x%x", result));
            PhDeleteFileWin32(newPackageFileName->Buffer);
            status = STATUS_UNSUCCESSFUL;
            goto CleanupExit;
        }

        for (revisionId = OldFirstRevisionId; revisionId <= NewFirstRevisionId; revisionId++)
        {
            mergePackageFileName = EnpFormatPackageName(Config, revisionId);
            status = PhDeleteFileWin32(mergePackageFileName->Buffer);

            if (!NT_SUCCESS(status) && status != STATUS_OBJECT_PATH_NOT_FOUND && status != STATUS_OBJECT_NAME_NOT_FOUND)
                MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to delete %s", mergePackageFileName->Buffer));

            PhDereferenceObject(mergePackageFileName);
        }

        status = EnpRenameFileWin32(NULL, newPackageFileName->Buffer, targetPackageFileName->Buffer);

        if (!NT_SUCCESS(status))
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to rename %s", newPackageFileName->Buffer));
    }

CleanupExit:
    if (targetPackageFileName)
        PhDereferenceObject(targetPackageFileName);
    if (basePackageFileName)
        PhDereferenceObject(basePackageFileName);
    if (basePackage)
        PkDereferencePackage(basePackage);
    if (newPackageFileName)
        PhDereferenceObject(newPackageFileName);
    if (pkNewPackageFileStream)
        PkDereferenceFileStream(pkNewPackageFileStream);
    if (actionList)
        PkDestroyActionList(actionList);

    return status;
}

HRESULT EnpMergePackageCallback(
    __in PK_PACKAGE_CALLBACK_MESSAGE Message,
    __in_opt PPK_ACTION Action,
    __in PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PEN_PACKAGE_CALLBACK_CONTEXT context = Context;

    switch (Message)
    {
    case PkFilterItemMessage:
        {
            PPK_PARAMETER_FILTER_ITEM filterItem = Parameter;

            if (context->Merge.IgnoreFileNames)
            {
                if (EnpFindInFileNameHashtable(context->Merge.IgnoreFileNames, &filterItem->Path))
                    filterItem->Reject = TRUE;
            }
        }
        break;
    case PkProgressMessage:
        {
            PPK_PARAMETER_PROGRESS progress = Parameter;
            PH_FORMAT format[3];

            if (progress->ProgressTotal != 0)
            {
                PhInitFormatS(&format[0], L"Compressing: ");
                PhInitFormatF(&format[1], (DOUBLE)progress->ProgressValue * 100 / progress->ProgressTotal, 2);
                format[1].Type |= FormatRightAlign;
                format[1].Width = 5;
                PhInitFormatC(&format[2], '%');

                context->MessageHandler(EN_MESSAGE_PROGRESS, PhFormat(format, 3, 0));
            }
        }
        break;
    }

    return S_OK;
}

NTSTATUS EnpUpdateDatabaseAfterTrim(
    __in PDB_DATABASE Database,
    __in ULONGLONG OldFirstRevisionId,
    __in ULONGLONG NewFirstRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    ULONGLONG revisionId;
    PDBF_FILE directory;
    PH_STRINGREF directoryName;
    WCHAR directoryNameBuffer[17];

    for (revisionId = OldFirstRevisionId; revisionId < NewFirstRevisionId; revisionId++)
    {
        EnpFormatRevisionId(revisionId, directoryNameBuffer);
        directoryName.Buffer = directoryNameBuffer;
        directoryName.Length = 16 * sizeof(WCHAR);
        status = DbCreateFile(Database, &directoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &directory);

        if (NT_SUCCESS(status))
        {
            // Delete the diff directory.
            DbUtDeleteDirectoryContents(Database, directory);
            status = DbDeleteFile(Database, directory);
            DbCloseFile(Database, directory);
        }

        if (!NT_SUCCESS(status))
            MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to delete %s directory", directoryNameBuffer));
    }

    PhInitializeEmptyStringRef(&directoryName);
    status = DbCreateFile(Database, &directoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &directory);

    if (NT_SUCCESS(status))
    {
        status = EnpUpdateDirectoryMinimumRevisionIds(Database, directory, NewFirstRevisionId);
        DbCloseFile(Database, directory);
    }

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to update revision IDs"));
        status = STATUS_NOT_ALL_ASSIGNED;
    }

    DbSetRevisionIdsDatabase(Database, NULL, &NewFirstRevisionId);

    return status;
}

NTSTATUS EnpUpdateDirectoryMinimumRevisionIds(
    __in PDB_DATABASE Database,
    __in PDBF_FILE Directory,
    __in ULONGLONG FirstRevisionId
    )
{
    NTSTATUS status;
    PDB_FILE_DIRECTORY_INFORMATION entries;
    ULONG numberOfEntries;
    ULONG i;
    PDBF_FILE file;
    DB_FILE_REVISION_ID_INFORMATION revisionIdInfo;

    status = DbQueryDirectoryFile(Database, Directory, &entries, &numberOfEntries);

    if (!NT_SUCCESS(status))
        return status;

    for (i = 0; i < numberOfEntries; i++)
    {
        if (entries[i].RevisionId < FirstRevisionId || (entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        {
            status = DbCreateFile(Database, &entries[i].FileName->sr, Directory, 0, DB_FILE_OPEN, 0, NULL, &file);

            if (NT_SUCCESS(status))
            {
                if (entries[i].RevisionId != 0 && entries[i].RevisionId < FirstRevisionId)
                {
                    revisionIdInfo.RevisionId = FirstRevisionId;
                    DbSetInformationFile(Database, file, DbFileRevisionIdInformation, &revisionIdInfo, sizeof(DB_FILE_REVISION_ID_INFORMATION));
                }

                if (entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                {
                    EnpUpdateDirectoryMinimumRevisionIds(Database, file, FirstRevisionId);
                }

                DbCloseFile(Database, file);
            }
        }
    }

    DbFreeQueryDirectoryFile(entries, numberOfEntries);

    return STATUS_SUCCESS;
}

BOOLEAN EnpRevisionEntryCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    )
{
    PEN_REVISION_ENTRY revisionEntry1 = Entry1;
    PEN_REVISION_ENTRY revisionEntry2 = Entry2;

    return revisionEntry1->RevisionId == revisionEntry2->RevisionId;
}

ULONG EnpRevisionEntryHashFunction(
    __in PVOID Entry
    )
{
    PEN_REVISION_ENTRY revisionEntry = Entry;

    return PhHashInt64(revisionEntry->RevisionId);
}

NTSTATUS EnpRestoreFromRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in_opt ULONGLONG RevisionId,
    __in PPH_STRINGREF RestoreToDirectory,
    __in_opt PPH_STRINGREF RestoreToName,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PH_STRINGREF fileName;
    ULONGLONG lastRevisionId;
    ULONGLONG firstRevisionId;
    ULONGLONG revisionId;
    ULONGLONG revisionIdOnFile;
    BOOLEAN restoreDirectoryFile;
    PH_STRINGREF directoryName;
    WCHAR directoryNameBuffer[17];
    PDBF_FILE directory;
    PDBF_FILE file;
    DB_FILE_BASIC_INFORMATION basicInfo;
    PPH_STRING newRestoreToDirectory;
    HANDLE directoryHandle;
    ULONG createStatus;

    fileName = *FileName;

    while (fileName.Length != 0 && fileName.Buffer[0] == '\\')
    {
        fileName.Buffer++;
        fileName.Length -= sizeof(WCHAR);
    }

    DbQueryRevisionIdsDatabase(Database, &lastRevisionId, &firstRevisionId);

    if (RevisionId == 0)
    {
        RevisionId = lastRevisionId;
    }
    else
    {
        if (RevisionId < firstRevisionId || RevisionId > lastRevisionId)
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Invalid revision ID '%I64u'", RevisionId));
            return STATUS_INVALID_PARAMETER;
        }
    }

    revisionIdOnFile = 0;
    restoreDirectoryFile = FALSE;

    // Perform virtual merges for just the specified file.
    for (revisionId = lastRevisionId; revisionId >= RevisionId; revisionId--)
    {
        if (revisionId == lastRevisionId)
        {
            PhInitializeStringRef(&directoryName, L"head");
        }
        else
        {
            EnpFormatRevisionId(revisionId, directoryNameBuffer);
            directoryName.Buffer = directoryNameBuffer;
            directoryName.Length = 16 * sizeof(WCHAR);
        }

        status = DbCreateFile(Database, &directoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &directory);

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open %.*s", directoryName.Length / sizeof(WCHAR), directoryName.Buffer));
            return status;
        }

        status = DbCreateFile(Database, &fileName, directory, 0, DB_FILE_OPEN, 0, NULL, &file);

        if (status == STATUS_OBJECT_PATH_NOT_FOUND || status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            // The file doesn't exist in this revision. Go to the next one.
            DbCloseFile(Database, directory);
            continue;
        }

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open %.*s", fileName.Length / sizeof(WCHAR), fileName.Buffer));
            DbCloseFile(Database, directory);
            return status;
        }

        status = DbQueryInformationFile(Database, file, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION));

        if (NT_SUCCESS(status))
        {
            if (basicInfo.Attributes & DB_FILE_ATTRIBUTE_DELETE_TAG)
            {
                revisionIdOnFile = 0;
                restoreDirectoryFile = FALSE;
            }
            else
            {
                if (basicInfo.Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                {
                    revisionIdOnFile = 0;
                    restoreDirectoryFile = TRUE;
                }
                else
                {
                    revisionIdOnFile = basicInfo.RevisionId;
                    restoreDirectoryFile = FALSE;
                }
            }
        }
        else
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to query %.*s", fileName.Length / sizeof(WCHAR), fileName.Buffer));
        }

        DbCloseFile(Database, file);
        DbCloseFile(Database, directory);
    }

    if (restoreDirectoryFile)
    {
        if (RestoreToName)
        {
            newRestoreToDirectory = EnpAppendComponentToPath(RestoreToDirectory, RestoreToName);
            status = PhCreateFileWin32Ex(
                &directoryHandle,
                newRestoreToDirectory->Buffer,
                FILE_GENERIC_READ,
                FILE_ATTRIBUTE_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                FILE_OPEN_IF,
                FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                &createStatus
                );

            if (NT_SUCCESS(status))
            {
                NtClose(directoryHandle);
            }

            RestoreToDirectory = &newRestoreToDirectory->sr;
        }
        else
        {
            newRestoreToDirectory = NULL;
        }

        if (NT_SUCCESS(status))
        {
            if (RevisionId == lastRevisionId)
                status = EnpRestoreDirectoryFromHead(Config, Database, Flags, &fileName, RestoreToDirectory, MessageHandler);
            else
                status = EnpRestoreDirectoryFromRevision(Config, Flags, &fileName, RevisionId, RestoreToDirectory, MessageHandler);
        }

        if (newRestoreToDirectory)
            PhDereferenceObject(newRestoreToDirectory);
    }
    else if (revisionIdOnFile != 0)
    {
        status = EnpRestoreSingleFileFromRevision(Config, Database, Flags, &fileName, revisionIdOnFile, RestoreToDirectory, RestoreToName, MessageHandler);
    }
    else
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"The file does not exist in revision %I64u", RevisionId));
        status = STATUS_OBJECT_NAME_NOT_FOUND;
    }

    return status;
}

NTSTATUS EnpRestoreSingleFileFromRevision(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in ULONGLONG RevisionId,
    __in PPH_STRINGREF RestoreToDirectory,
    __in_opt PPH_STRINGREF RestoreToName,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PH_STRINGREF path;
    PH_STRINGREF name;
    PPH_STRING fileName;
    PPH_HASHTABLE fileNames;

    if (!RestoreToName)
    {
        if (!PhSplitStringRefAtLastChar(FileName, '\\', &path, &name))
            name = *FileName;

        RestoreToName = &name;
    }

    fileNames = EnpCreateFileNameHashtable();

    fileName = PhCreateStringEx(FileName->Buffer, FileName->Length);
    EnpAddToFileNameHashtable(fileNames, fileName);
    PhDereferenceObject(fileName);

    status = EnpExtractFromPackage(Config, Flags, RevisionId, NULL, fileNames, RestoreToDirectory, RestoreToName, MessageHandler);

    EnpDestroyFileNameHashtable(fileNames);

    return status;
}

NTSTATUS EnpRestoreDirectoryFromHead(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in PPH_STRINGREF RestoreToDirectory,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PDBF_FILE headDirectory;
    PH_STRINGREF headDirectoryName;
    PDBF_FILE file;
    PPH_STRING fileName;
    PPH_HASHTABLE revisionEntries;
    PH_HASHTABLE_ENUM_CONTEXT enumContext;
    PEN_REVISION_ENTRY revisionEntry;
    PH_HASHTABLE_ENUM_CONTEXT directoryEnumContext;
    PPH_STRING *directoryNamePtr;
    PPH_STRING directoryName;
    PH_STRINGREF relativeName;
    PPH_STRING newDirectoryName;

    PhInitializeStringRef(&headDirectoryName, L"head");
    status = DbCreateFile(Database, &headDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &headDirectory);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to open HEAD"));
        return status;
    }

    status = DbCreateFile(Database, FileName, headDirectory, 0, DB_FILE_OPEN, 0, NULL, &file);
    DbCloseFile(Database, headDirectory);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open %.*s", FileName->Length / sizeof(WCHAR), FileName->Buffer));
        return status;
    }

    fileName = PhCreateStringEx(FileName->Buffer, FileName->Length);

    revisionEntries = PhCreateHashtable(
        sizeof(EN_REVISION_ENTRY),
        EnpRevisionEntryCompareFunction,
        EnpRevisionEntryHashFunction,
        10
        );

    MessageHandler(EN_MESSAGE_PROGRESS, PhCreateString(L"Creating file list"));
    status = EnpAddRestoreFileNamesFromDirectory(Database, revisionEntries, file, fileName);
    DbCloseFile(Database, file);
    PhDereferenceObject(fileName);

    // Create all directories.

    MessageHandler(EN_MESSAGE_PROGRESS, PhCreateString(L"Creating directories"));
    PhBeginEnumHashtable(revisionEntries, &enumContext);

    while (revisionEntry = PhNextEnumHashtable(&enumContext))
    {
        PhBeginEnumHashtable(revisionEntry->DirectoryNames, &directoryEnumContext);

        while (directoryNamePtr = PhNextEnumHashtable(&directoryEnumContext))
        {
            directoryName = *directoryNamePtr;

            if (!PhStartsWithStringRef(&directoryName->sr, FileName, TRUE))
            {
                MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Wrong prefix: %s", directoryName->Buffer));
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            relativeName.Buffer = (PWSTR)((PCHAR)directoryName->Buffer + FileName->Length);
            relativeName.Length = directoryName->Length - FileName->Length;
            newDirectoryName = EnpAppendComponentToPath(RestoreToDirectory, &relativeName);

            SHCreateDirectory(NULL, newDirectoryName->Buffer);
            PhDereferenceObject(newDirectoryName);
        }

        if (!NT_SUCCESS(status))
            break;
    }

    // Extract the files.

    PhBeginEnumHashtable(revisionEntries, &enumContext);

    while (revisionEntry = PhNextEnumHashtable(&enumContext))
    {
        if (NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_PROGRESS, PhFormatString(L"Processing revision %I64u", revisionEntry->RevisionId));
            status = EnpExtractFromPackage(Config, Flags, revisionEntry->RevisionId, FileName, revisionEntry->FileNames, RestoreToDirectory, NULL, MessageHandler);
        }

        EnpDestroyFileNameHashtable(revisionEntry->FileNames);
        EnpDestroyFileNameHashtable(revisionEntry->DirectoryNames);
    }

    PhDereferenceObject(revisionEntries);

    return status;
}

NTSTATUS EnpAddRestoreFileNamesFromDirectory(
    __in PDB_DATABASE Database,
    __in PPH_HASHTABLE RevisionEntries,
    __in PDBF_FILE Directory,
    __in PPH_STRING DirectoryName
    )
{
    NTSTATUS status;
    PDB_FILE_DIRECTORY_INFORMATION entries;
    ULONG numberOfEntries;
    ULONG i;
    PPH_STRING fileName;
    PDBF_FILE file;
    PEN_REVISION_ENTRY revisionEntry;
    EN_REVISION_ENTRY localRevisionEntry;
    BOOLEAN added;

    // This function is similar to EnpAddMergeFileNamesFromDirectory, except that directories are added to a separate hashtable.

    status = DbQueryDirectoryFile(Database, Directory, &entries, &numberOfEntries);

    if (!NT_SUCCESS(status))
        return status;

    for (i = 0; i < numberOfEntries; i++)
    {
        fileName = EnpAppendComponentToPath(&DirectoryName->sr, &entries[i].FileName->sr);

        localRevisionEntry.RevisionId = entries[i].RevisionId;
        localRevisionEntry.FileNames = NULL;
        localRevisionEntry.DirectoryNames = NULL;
        revisionEntry = PhAddEntryHashtableEx(RevisionEntries, &localRevisionEntry, &added);

        if (added)
        {
            revisionEntry->FileNames = EnpCreateFileNameHashtable();
            revisionEntry->DirectoryNames = EnpCreateFileNameHashtable();
        }

        if (entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
        {
            EnpAddToFileNameHashtable(revisionEntry->DirectoryNames, fileName);

            status = DbCreateFile(Database, &entries[i].FileName->sr, Directory, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &file);

            if (NT_SUCCESS(status))
            {
                status = EnpAddRestoreFileNamesFromDirectory(Database, RevisionEntries, file, fileName);
            }

            if (!NT_SUCCESS(status))
            {
                DbCloseFile(Database, file);
                PhDereferenceObject(fileName);
                break;
            }

            DbCloseFile(Database, file);
        }
        else
        {
            EnpAddToFileNameHashtable(revisionEntry->FileNames, fileName);
        }

        PhDereferenceObject(fileName);
    }

    DbFreeQueryDirectoryFile(entries, numberOfEntries);

    return status;
}

NTSTATUS EnpRestoreDirectoryFromRevision(
    __in PBK_CONFIG Config,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in ULONGLONG RevisionId,
    __in PPH_STRINGREF RestoreToDirectory,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PH_STRINGREF databaseName;
    PPH_STRING databaseFileName;
    PPH_STRING tempDatabaseFileName;
    PDB_DATABASE tempDatabase;
    PDBF_FILE headDirectory;
    PDBF_FILE file;

    PhInitializeStringRef(&databaseName, L"\\" EN_DATABASE_NAME);
    databaseFileName = PhConcatStringRef2(&Config->DestinationDirectory->sr, &databaseName);
    tempDatabaseFileName = EnpFormatTempDatabaseFileName(Config, FALSE);

    status = EnpCopyFileWin32(databaseFileName->Buffer, tempDatabaseFileName->Buffer, FILE_ATTRIBUTE_TEMPORARY, FALSE);

    if (!NT_SUCCESS(status))
    {
        PhDereferenceObject(tempDatabaseFileName);
        PhDereferenceObject(databaseFileName);
        return status;
    }

    status = DbOpenDatabase(&tempDatabase, tempDatabaseFileName->Buffer, FALSE, 0);

    if (NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_PROGRESS, PhFormatString(L"Merging until revision %I64u", RevisionId));
        status = EnpMergeToHeadUntilRevision(tempDatabase, RevisionId, MessageHandler, &headDirectory);

        if (NT_SUCCESS(status))
        {
            status = DbCreateFile(tempDatabase, FileName, headDirectory, 0, DB_FILE_OPEN, 0, NULL, &file);

            if (NT_SUCCESS(status))
            {
                status = EnpRestoreDirectoryFromHead(Config, tempDatabase, Flags, FileName, RestoreToDirectory, MessageHandler);
                DbCloseFile(tempDatabase, file);
            }
            else
            {
                MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"The file does not exist in the specified revision."));
            }

            DbCloseFile(tempDatabase, headDirectory);
        }

        DbCloseDatabase(tempDatabase);
    }

    PhDeleteFileWin32(tempDatabaseFileName->Buffer);
    PhDereferenceObject(tempDatabaseFileName);
    PhDereferenceObject(databaseFileName);

    return status;
}

NTSTATUS EnpMergeToHeadUntilRevision(
    __in PDB_DATABASE Database,
    __in ULONGLONG TargetRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PDBF_FILE *HeadDirectory
    )
{
    NTSTATUS status;
    ULONGLONG revisionId;
    PDBF_FILE headDirectory;
    PH_STRINGREF headDirectoryName;
    PDBF_FILE diffDirectory;
    PH_STRINGREF diffDirectoryName;
    WCHAR diffDirectoryNameBuffer[17];

    PhInitializeStringRef(&headDirectoryName, L"head");
    status = DbCreateFile(Database, &headDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &headDirectory);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to open HEAD directory"));
        return status;
    }

    DbQueryRevisionIdsDatabase(Database, &revisionId, NULL);
    revisionId--;

    while (revisionId >= TargetRevisionId)
    {
        // Open the diff directory.

        EnpFormatRevisionId(revisionId, diffDirectoryNameBuffer);
        diffDirectoryName.Buffer = diffDirectoryNameBuffer;
        diffDirectoryName.Length = 16 * sizeof(WCHAR);
        status = DbCreateFile(Database, &diffDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &diffDirectory);

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open %s directory", diffDirectoryNameBuffer));
            break;
        }

        // Perform the merge.
        status = EnpMergeDirectoryToHead(Database, headDirectory, diffDirectory, MessageHandler);

        DbCloseFile(Database, diffDirectory);

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to merge revision %I64u with HEAD", revisionId));
            break;
        }

        revisionId--;
    }

    if (HeadDirectory)
        *HeadDirectory = headDirectory;
    else
        DbCloseFile(Database, headDirectory);

    return status;
}

NTSTATUS EnpExtractFromPackage(
    __in PBK_CONFIG Config,
    __in ULONG Flags,
    __in ULONGLONG RevisionId,
    __in_opt PPH_STRINGREF BaseFileName,
    __in PPH_HASHTABLE FileNames,
    __in PPH_STRINGREF RestoreToDirectory,
    __in_opt PPH_STRINGREF RestoreToName,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    HRESULT result;
    PPH_STRING packageFileName;
    PPH_FILE_STREAM fileStream;
    PPK_FILE_STREAM pkFileStream;
    EN_PACKAGE_CALLBACK_CONTEXT context;
    PPK_PACKAGE package;
    PPK_ACTION_LIST actionList;

    packageFileName = EnpFormatPackageName(Config, RevisionId);
    status = PhCreateFileStream(&fileStream, packageFileName->Buffer, FILE_GENERIC_READ, FILE_SHARE_READ, FILE_OPEN, 0);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open %s", packageFileName->Buffer));
        PhDereferenceObject(packageFileName);
        return status;
    }

    pkFileStream = PkCreateFileStream(fileStream);
    PhDereferenceObject(fileStream);

    context.Config = Config;
    context.MessageHandler = MessageHandler;
    context.Restore.Flags = Flags;
    context.Restore.RestoreToDirectory = RestoreToDirectory;
    context.Restore.RestoreToName = RestoreToName;
    context.Restore.BaseFileName = BaseFileName;
    context.Restore.FileNames = FileNames;

    actionList = PkCreateActionList();

    result = PkOpenPackageWithFilter(
        pkFileStream,
        actionList,
        EnpRestorePackageCallback,
        &context,
        &package
        );

    if (SUCCEEDED(result))
    {
        result = PkExtractPackage(
            package,
            actionList,
            EnpRestorePackageCallback,
            &context
            );
        PkDereferencePackage(package);

        if (!SUCCEEDED(result))
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to extract from package %s", packageFileName->Buffer));
    }
    else
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to open package %s", packageFileName->Buffer));
    }

    PkDestroyActionList(actionList);

    PkDereferenceFileStream(pkFileStream);
    PhDereferenceObject(packageFileName);

    if (!SUCCEEDED(result))
        status = STATUS_UNSUCCESSFUL;

    return status;
}

HRESULT EnpRestorePackageCallback(
    __in PK_PACKAGE_CALLBACK_MESSAGE Message,
    __in_opt PPK_ACTION Action,
    __in PVOID Parameter,
    __in_opt PVOID Context
    )
{
    NTSTATUS status;
    PEN_PACKAGE_CALLBACK_CONTEXT context = Context;

    switch (Message)
    {
    case PkFilterItemMessage:
        {
            PPK_PARAMETER_FILTER_ITEM filterItem = Parameter;
            PPH_STRING fileName;

            fileName = EnpFindInFileNameHashtable(context->Restore.FileNames, &filterItem->Path);

            if (!fileName)
            {
                filterItem->Reject = TRUE;
                return S_OK;
            }

            filterItem->NewContext = fileName;
        }
        break;
    case PkGetStreamMessage:
        {
            PPK_PARAMETER_GET_STREAM getStream = Parameter;
            PPH_STRING fileName;
            PPH_STRING outFileName;
            PH_STRINGREF relativeName;
            PPH_FILE_STREAM outFileStream;
            PPK_FILE_STREAM pkOutFileStream;
            FILE_BASIC_INFORMATION basicInfo;
            IO_STATUS_BLOCK iosb;

            fileName = Action->Context;

            if (context->Restore.BaseFileName)
            {
                if (!PhStartsWithStringRef(&fileName->sr, context->Restore.BaseFileName, TRUE))
                {
                    context->MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Wrong prefix: %s", fileName->Buffer));
                    return E_FAIL;
                }

                relativeName.Buffer = (PWSTR)((PCHAR)fileName->Buffer + context->Restore.BaseFileName->Length);
                relativeName.Length = fileName->Length - context->Restore.BaseFileName->Length;
                outFileName = EnpAppendComponentToPath(context->Restore.RestoreToDirectory, &relativeName);
            }
            else
            {
                outFileName = EnpAppendComponentToPath(context->Restore.RestoreToDirectory, context->Restore.RestoreToName);
            }

            status = PhCreateFileStream(
                &outFileStream,
                outFileName->Buffer,
                FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                0,
                (context->Restore.Flags & EN_RESTORE_OVERWRITE_FILES) ? FILE_OVERWRITE_IF : FILE_CREATE,
                0
                );

            if (!NT_SUCCESS(status))
            {
                if (status == STATUS_OBJECT_NAME_COLLISION)
                    context->MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"File already exists (try --force): %s", outFileName->Buffer));
                else
                    context->MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to create %s", outFileName->Buffer));

                PhDereferenceObject(outFileName);
                return E_FAIL;
            }

            if (NT_SUCCESS(status = NtQueryInformationFile(outFileStream->FileHandle, &iosb, &basicInfo, sizeof(FILE_BASIC_INFORMATION), FileBasicInformation)))
            {
                if (getStream->FileInformation.CreationTime.QuadPart != 0)
                    basicInfo.CreationTime = getStream->FileInformation.CreationTime;

                if (getStream->FileInformation.LastAccessTime.QuadPart != 0)
                    basicInfo.LastAccessTime = getStream->FileInformation.LastAccessTime;

                if (getStream->FileInformation.LastWriteTime.QuadPart != 0)
                    basicInfo.LastWriteTime = getStream->FileInformation.LastWriteTime;

                if (getStream->FileInformation.ChangeTime.QuadPart != 0)
                    basicInfo.ChangeTime = getStream->FileInformation.ChangeTime;

                basicInfo.FileAttributes = getStream->FileInformation.FileAttributes & ~FILE_ATTRIBUTE_NORMAL;

                status = NtSetInformationFile(outFileStream->FileHandle, &iosb, &basicInfo, sizeof(FILE_BASIC_INFORMATION), FileBasicInformation);
            }

            if (!NT_SUCCESS(status))
                context->MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to set attributes on %s", outFileName->Buffer));

            pkOutFileStream = PkCreateFileStream(outFileStream);
            PhDereferenceObject(outFileStream);

            getStream->FileStream = pkOutFileStream;

            PhDereferenceObject(outFileName);
        }
        break;
    case PkProgressMessage:
        {
            PPK_PARAMETER_PROGRESS progress = Parameter;
            PH_FORMAT format[3];

            if (progress->ProgressTotal != 0)
            {
                PhInitFormatS(&format[0], L"Extracting: ");
                PhInitFormatF(&format[1], (DOUBLE)progress->ProgressValue * 100 / progress->ProgressTotal, 2);
                format[1].Type |= FormatRightAlign;
                format[1].Width = 5;
                PhInitFormatC(&format[2], '%');

                context->MessageHandler(EN_MESSAGE_PROGRESS, PhFormat(format, 3, 0));
            }
        }
        break;
    }

    return S_OK;
}

NTSTATUS EnpQueryFileRevisions(
    __in PDB_DATABASE Database,
    __in PPH_STRINGREF FileName,
    __in ULONGLONG FirstRevisionId,
    __in ULONGLONG LastRevisionId,
    __out PEN_FILE_REVISION_INFORMATION *Entries,
    __out PULONG NumberOfEntries,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    PEN_FILE_REVISION_INFORMATION entries;
    ULONG allocatedEntries;
    ULONG numberOfEntries;
    ULONGLONG revisionId;
    PDBF_FILE directory;
    PH_STRINGREF directoryName;
    WCHAR directoryNameBuffer[17];
    PDBF_FILE file;
    DB_FILE_BASIC_INFORMATION basicInfo;
    DB_FILE_DATA_INFORMATION dataInfo;
    PEN_FILE_REVISION_INFORMATION entry;

    allocatedEntries = 16;
    entries = PhAllocate(allocatedEntries * sizeof(EN_FILE_REVISION_INFORMATION));
    numberOfEntries = 0;

    for (revisionId = LastRevisionId; revisionId >= FirstRevisionId; revisionId--)
    {
        if (revisionId == LastRevisionId)
        {
            PhInitializeStringRef(&directoryName, L"head");
        }
        else
        {
            EnpFormatRevisionId(revisionId, directoryNameBuffer);
            directoryName.Buffer = directoryNameBuffer;
            directoryName.Length = 16 * sizeof(WCHAR);
        }

        status = DbCreateFile(Database, &directoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_ATTRIBUTE_DIRECTORY, NULL, &directory);

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to open %.*s", directoryName.Length / sizeof(WCHAR), directoryName.Buffer));
            continue;
        }

        status = DbCreateFile(Database, FileName, directory, 0, DB_FILE_OPEN, 0, NULL, &file);

        if (NT_SUCCESS(status))
        {
            status = DbQueryInformationFile(Database, file, DbFileBasicInformation, &basicInfo, sizeof(DB_FILE_BASIC_INFORMATION));

            if (NT_SUCCESS(status))
            {
                if (basicInfo.Attributes & DB_FILE_ATTRIBUTE_DELETE_TAG)
                {
                    DbCloseFile(Database, file);
                    DbCloseFile(Database, directory);
                    continue;
                }

                if (!(basicInfo.Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
                    status = DbQueryInformationFile(Database, file, DbFileDataInformation, &dataInfo, sizeof(DB_FILE_DATA_INFORMATION));
                else
                    memset(&dataInfo, 0, sizeof(DB_FILE_DATA_INFORMATION));
            }

            if (NT_SUCCESS(status))
            {
                if (numberOfEntries == allocatedEntries)
                {
                    allocatedEntries *= 2;
                    entries = PhReAllocate(entries, allocatedEntries * sizeof(EN_FILE_REVISION_INFORMATION));
                }

                entry = &entries[numberOfEntries++];
                entry->Attributes = basicInfo.Attributes;
                entry->RevisionId = revisionId;
                entry->TimeStamp = basicInfo.TimeStamp;
                entry->EndOfFile = dataInfo.EndOfFile;
                entry->LastBackupTime = dataInfo.LastBackupTime;

                if (!(basicInfo.Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
                    entry->RevisionId = basicInfo.RevisionId;
            }
            else
            {
                MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to query information in %.*s", directoryName.Length / sizeof(WCHAR), directoryName.Buffer));
            }

            DbCloseFile(Database, file);
        }

        DbCloseFile(Database, directory);
    }

    *Entries = entries;
    *NumberOfEntries = numberOfEntries;

    return STATUS_SUCCESS;
}

NTSTATUS EnpCompareRevisions(
    __in PBK_CONFIG Config,
    __in PDB_DATABASE Database,
    __in ULONGLONG BaseRevisionId,
    __in_opt ULONGLONG TargetRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    ULONGLONG lastRevisionId;
    ULONGLONG firstRevisionId;
    PH_STRINGREF databaseName;
    PPH_STRING databaseFileName;
    PPH_STRING tempBaseDatabaseFileName;
    PPH_STRING tempTargetDatabaseFileName;
    BOOLEAN tempBaseDatabaseCreated;
    BOOLEAN tempTargetDatabaseCreated;
    PDB_DATABASE tempBaseDatabase;
    PDB_DATABASE tempTargetDatabase;
    PDBF_FILE baseHeadDirectory;
    PDBF_FILE targetHeadDirectory;
    PH_STRINGREF headDirectoryName;
    PPH_STRING emptyFileName;
    ULONGLONG numberOfChanges;

    DbQueryRevisionIdsDatabase(Database, &lastRevisionId, &firstRevisionId);

    if (TargetRevisionId == 0)
    {
        TargetRevisionId = lastRevisionId;
    }
    else if (TargetRevisionId < firstRevisionId || TargetRevisionId > lastRevisionId)
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Invalid base revision ID '%I64u'", BaseRevisionId));
        return STATUS_INVALID_PARAMETER;
    }

    if (BaseRevisionId < firstRevisionId || BaseRevisionId > lastRevisionId)
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Invalid base revision ID '%I64u'", BaseRevisionId));
        return STATUS_INVALID_PARAMETER;
    }

    if (BaseRevisionId >= TargetRevisionId)
    {
        MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Base revision ID '%I64u' must be less than target revision ID '%I64u'", BaseRevisionId, TargetRevisionId));
        return STATUS_INVALID_PARAMETER;
    }

    PhInitializeStringRef(&databaseName, L"\\" EN_DATABASE_NAME);
    databaseFileName = PhConcatStringRef2(&Config->DestinationDirectory->sr, &databaseName);
    tempBaseDatabaseFileName = EnpFormatTempDatabaseFileName(Config, FALSE);
    tempTargetDatabaseFileName = EnpFormatTempDatabaseFileName(Config, FALSE);
    tempBaseDatabaseCreated = FALSE;
    tempTargetDatabaseCreated = FALSE;
    tempBaseDatabase = NULL;
    tempTargetDatabase = NULL;
    baseHeadDirectory = NULL;
    targetHeadDirectory = NULL;

    // Create a temporary "base" database and merge to the base revision ID.

    MessageHandler(EN_MESSAGE_PROGRESS, PhCreateString(L"Processing base"));

    status = EnpCopyFileWin32(databaseFileName->Buffer, tempBaseDatabaseFileName->Buffer, FILE_ATTRIBUTE_TEMPORARY, FALSE);

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    tempBaseDatabaseCreated = TRUE;
    status = DbOpenDatabase(&tempBaseDatabase, tempBaseDatabaseFileName->Buffer, FALSE, 0);

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    status = EnpMergeToHeadUntilRevision(tempBaseDatabase, BaseRevisionId, MessageHandler, &baseHeadDirectory);

    if (!NT_SUCCESS(status))
    {
        MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to merge base revisions"));
        goto CleanupExit;
    }

    // If necessary, create a temporary "target" database and merge to the target revision ID.

    MessageHandler(EN_MESSAGE_PROGRESS, PhCreateString(L"Processing target"));

    if (TargetRevisionId == lastRevisionId)
    {
        PhInitializeStringRef(&headDirectoryName, L"head");
        status = DbCreateFile(Database, &headDirectoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &targetHeadDirectory);

        if (!NT_SUCCESS(status))
            goto CleanupExit;
    }
    else
    {
        status = EnpCopyFileWin32(databaseFileName->Buffer, tempTargetDatabaseFileName->Buffer, FILE_ATTRIBUTE_TEMPORARY, FALSE);

        if (!NT_SUCCESS(status))
            goto CleanupExit;

        tempTargetDatabaseCreated = TRUE;
        status = DbOpenDatabase(&tempTargetDatabase, tempTargetDatabaseFileName->Buffer, FALSE, 0);

        if (!NT_SUCCESS(status))
            goto CleanupExit;

        status = EnpMergeToHeadUntilRevision(tempTargetDatabase, TargetRevisionId, MessageHandler, &targetHeadDirectory);

        if (!NT_SUCCESS(status))
        {
            MessageHandler(EN_MESSAGE_ERROR, PhCreateString(L"Unable to merge target revisions"));
            goto CleanupExit;
        }
    }

    emptyFileName = PhReferenceEmptyString();
    numberOfChanges = 0;
    status = EnpDiffDirectoryCompareRevisions(
        tempBaseDatabase,
        tempTargetDatabase ? tempTargetDatabase : Database,
        baseHeadDirectory,
        targetHeadDirectory,
        emptyFileName,
        &numberOfChanges,
        MessageHandler
        );
    PhDereferenceObject(emptyFileName);

    if (numberOfChanges != 0)
        MessageHandler(EN_MESSAGE_INFORMATION, PhFormatString(L"%I64u change(s)", numberOfChanges));

CleanupExit:
    if (baseHeadDirectory)
        DbCloseFile(tempBaseDatabase, baseHeadDirectory);
    if (targetHeadDirectory)
        DbCloseFile(tempTargetDatabase ? tempTargetDatabase : Database, targetHeadDirectory);
    if (tempBaseDatabase)
        DbCloseDatabase(tempBaseDatabase);
    if (tempTargetDatabase)
        DbCloseDatabase(tempTargetDatabase);
    if (tempBaseDatabaseCreated)
        PhDeleteFileWin32(tempBaseDatabaseFileName->Buffer);
    if (tempTargetDatabaseCreated)
        PhDeleteFileWin32(tempTargetDatabaseFileName->Buffer);

    PhDereferenceObject(tempBaseDatabaseFileName);
    PhDereferenceObject(tempTargetDatabaseFileName);
    PhDereferenceObject(databaseFileName);

    return status;
}

NTSTATUS EnpDiffDirectoryCompareRevisions(
    __in_opt PDB_DATABASE BaseDatabase,
    __in PDB_DATABASE TargetDatabase,
    __in_opt PDBF_FILE BaseDirectory,
    __in PDBF_FILE TargetDirectory,
    __in PPH_STRING FileName,
    __inout PULONGLONG NumberOfChanges,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;
    ULONG i;
    PPH_STRING fileNamePrefix;
    PDB_FILE_DIRECTORY_INFORMATION baseEntries;
    ULONG numberOfBaseEntries;
    PDB_FILE_DIRECTORY_INFORMATION targetEntries;
    ULONG numberOfTargetEntries;
    PPH_HASHTABLE baseHashtable;
    PPH_HASHTABLE targetHashtable;
    PDB_FILE_DIRECTORY_INFORMATION entry;
    PDB_FILE_DIRECTORY_INFORMATION otherEntry;
    BOOLEAN modified;
    BOOLEAN switched;
    PDBF_FILE baseDirectory;
    PDBF_FILE targetDirectory;
    PPH_STRING fullFileName;

    if (FileName->Length != 0)
        fileNamePrefix = PhConcatStringRef2(&FileName->sr, &EnpBackslashString);
    else
        fileNamePrefix = PhReferenceEmptyString();

    baseEntries = NULL;
    targetEntries = NULL;
    baseHashtable = NULL;
    targetHashtable = NULL;

    if (BaseDirectory)
    {
        status = DbQueryDirectoryFile(BaseDatabase, BaseDirectory, &baseEntries, &numberOfBaseEntries);

        if (!NT_SUCCESS(status))
            goto CleanupExit;
    }
    else
    {
        baseEntries = NULL;
        numberOfBaseEntries = 0;
    }

    status = DbQueryDirectoryFile(TargetDatabase, TargetDirectory, &targetEntries, &numberOfTargetEntries);

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    baseHashtable = PhCreateHashtable(
        sizeof(PDB_FILE_DIRECTORY_INFORMATION),
        EnpDirectoryEntryCompareFunction,
        EnpDirectoryEntryHashFunction,
        numberOfBaseEntries
        );
    entry = baseEntries;

    for (i = 0; i < numberOfBaseEntries; i++)
    {
        PhAddEntryHashtable(baseHashtable, &entry);
        entry++;
    }

    targetHashtable = PhCreateHashtable(
        sizeof(PDB_FILE_DIRECTORY_INFORMATION),
        EnpDirectoryEntryCompareFunction,
        EnpDirectoryEntryHashFunction,
        numberOfTargetEntries
        );
    entry = targetEntries;

    for (i = 0; i < numberOfTargetEntries; i++)
    {
        PhAddEntryHashtable(targetHashtable, &entry);
        entry++;
    }

    // Detect files/directories that have been deleted.

    entry = baseEntries;

    for (i = 0; i < numberOfBaseEntries; i++)
    {
        if (!EnpFindDirectoryEntry(targetHashtable, entry->FileName))
        {
            // Deleted file
            (*NumberOfChanges)++;
            MessageHandler(EN_MESSAGE_INFORMATION, PhFormatString(L"- %s%s", fileNamePrefix->Buffer, entry->FileName->Buffer));
        }

        entry++;
    }

    // Detect files/directories that have been added or modified.

    entry = targetEntries;

    for (i = 0; i < numberOfTargetEntries; i++)
    {
        otherEntry = EnpFindDirectoryEntry(baseHashtable, entry->FileName);

        if (otherEntry)
        {
            modified = FALSE;
            switched = FALSE;

            if ((entry->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY) != (otherEntry->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
            {
                modified = TRUE;
                switched = TRUE;
            }

            if (!modified && !(entry->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
            {
                if (entry->EndOfFile.QuadPart != otherEntry->EndOfFile.QuadPart)
                    modified = TRUE;
                if (entry->LastBackupTime.QuadPart != otherEntry->LastBackupTime.QuadPart)
                    modified = TRUE;
            }

            if (modified)
            {
                // Modified file
                // or switched file (file has become directory or directory has become file)
                (*NumberOfChanges)++;
                MessageHandler(EN_MESSAGE_INFORMATION, PhFormatString(L"%c %s%s", switched ? 's' : 'm', fileNamePrefix->Buffer, entry->FileName->Buffer));
            }
        }
        else
        {
            // Added file
            (*NumberOfChanges)++;
            MessageHandler(EN_MESSAGE_INFORMATION, PhFormatString(L"+ %s%s", fileNamePrefix->Buffer, entry->FileName->Buffer));
        }

        if (entry->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
        {
            baseDirectory = NULL;
            targetDirectory = NULL;

            if (otherEntry && (otherEntry->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
            {
                status = DbCreateFile(BaseDatabase, &otherEntry->FileName->sr, BaseDirectory, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &baseDirectory);

                if (!NT_SUCCESS(status))
                    MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to open base directory %s%s", fileNamePrefix->Buffer, otherEntry->FileName->Buffer));
            }

            if (NT_SUCCESS(status))
            {
                status = DbCreateFile(TargetDatabase, &entry->FileName->sr, TargetDirectory, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &targetDirectory);

                if (!NT_SUCCESS(status))
                    MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Unable to open target directory %s%s", fileNamePrefix->Buffer, entry->FileName->Buffer));
            }

            if (NT_SUCCESS(status))
            {
                fullFileName = PhConcatStringRef2(&fileNamePrefix->sr, &entry->FileName->sr);
                EnpDiffDirectoryCompareRevisions(BaseDatabase, TargetDatabase, baseDirectory, targetDirectory, fullFileName, NumberOfChanges, MessageHandler);
                PhDereferenceObject(fullFileName);
            }

            if (baseDirectory)
                DbCloseFile(BaseDatabase, baseDirectory);
            if (targetDirectory)
                DbCloseFile(TargetDatabase, targetDirectory);

            status = STATUS_SUCCESS;
        }

        entry++;
    }

CleanupExit:
    if (baseHashtable)
        PhDereferenceObject(baseHashtable);
    if (targetHashtable)
        PhDereferenceObject(targetHashtable);
    if (baseEntries)
        DbFreeQueryDirectoryFile(baseEntries, numberOfBaseEntries);
    if (targetEntries)
        DbFreeQueryDirectoryFile(targetEntries, numberOfTargetEntries);

    PhDereferenceObject(fileNamePrefix);

    return status;
}

PPH_HASHTABLE EnpCreateFileNameHashtable(
    VOID
    )
{
    return PhCreateHashtable(
        sizeof(PPH_STRING),
        EnpFileNameCompareFunction,
        EnpFileNameHashFunction,
        64
        );
}

VOID EnpDestroyFileNameHashtable(
    __in PPH_HASHTABLE Hashtable
    )
{
    PH_HASHTABLE_ENUM_CONTEXT enumContext;
    PPH_STRING *stringPtr;

    PhBeginEnumHashtable(Hashtable, &enumContext);

    while (stringPtr = PhNextEnumHashtable(&enumContext))
        PhDereferenceObject(*stringPtr);

    PhDereferenceObject(Hashtable);
}

VOID EnpAddToFileNameHashtable(
    __in PPH_HASHTABLE Hashtable,
    __in PPH_STRING FileName
    )
{
    BOOLEAN added;

    PhAddEntryHashtableEx(Hashtable, &FileName, &added);

    if (added)
        PhReferenceObject(FileName);
}

PPH_STRING EnpFindInFileNameHashtable(
    __in PPH_HASHTABLE Hashtable,
    __in PPH_STRINGREF FileName
    )
{
    PH_STRING lookupString;
    PPH_STRING lookupStringPtr = &lookupString;
    PPH_STRING *string;

    lookupString.sr = *FileName;
    string = PhFindEntryHashtable(Hashtable, &lookupStringPtr);

    if (string)
        return *string;
    else
        return NULL;
}

BOOLEAN EnpRemoveFromFileNameHashtable(
    __in PPH_HASHTABLE Hashtable,
    __in PPH_STRINGREF FileName
    )
{
    PH_STRING lookupString;
    PPH_STRING lookupStringPtr = &lookupString;

    lookupString.sr = *FileName;
    return PhRemoveEntryHashtable(Hashtable, &lookupStringPtr);
}

BOOLEAN EnpFileNameCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    )
{
    PPH_STRING string1 = *(PPH_STRING *)Entry1;
    PPH_STRING string2 = *(PPH_STRING *)Entry2;

    return PhEqualStringRef(&string1->sr, &string2->sr, TRUE);
}

ULONG EnpFileNameHashFunction(
    __in PVOID Entry
    )
{
    PPH_STRING string = *(PPH_STRING *)Entry;

    return DbHashName(string->Buffer, string->Length / sizeof(WCHAR));
}

PEN_FILEINFO EnpCreateFileInfo(
    __in_opt PEN_FILEINFO Parent,
    __in_opt PPH_STRINGREF Name,
    __in_opt PPH_STRINGREF SourceName
    )
{
    PEN_FILEINFO fileInfo;

    fileInfo = PhAllocate(sizeof(EN_FILEINFO));
    memset(fileInfo, 0, sizeof(EN_FILEINFO));
    fileInfo->Directory = TRUE;

    if (Parent)
    {
        fileInfo->Parent = Parent;
        fileInfo->DiffFlags = Parent->DiffFlags;
    }

    if (Parent && Name)
    {
        fileInfo->Name = PhCreateStringEx(Name->Buffer, Name->Length);
        fileInfo->FullFileName = EnpAppendComponentToPath(&Parent->FullFileName->sr, Name);
        fileInfo->FullSourceFileName = EnpAppendComponentToPath(&Parent->FullSourceFileName->sr, SourceName);

        fileInfo->Key = fileInfo->Name->sr;
    }
    else
    {
        fileInfo->Name = PhReferenceEmptyString();
        fileInfo->FullFileName = PhReferenceEmptyString();
        fileInfo->FullSourceFileName = PhReferenceEmptyString();
    }

    return fileInfo;
}

BOOLEAN EnpFileInfoCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    )
{
    PEN_FILEINFO fileInfo1 = *(PEN_FILEINFO *)Entry1;
    PEN_FILEINFO fileInfo2 = *(PEN_FILEINFO *)Entry2;

    return PhEqualStringRef(&fileInfo1->Key, &fileInfo2->Key, TRUE);
}

ULONG EnpFileInfoHashFunction(
    __in PVOID Entry
    )
{
    PEN_FILEINFO fileInfo = *(PEN_FILEINFO *)Entry;

    return DbHashName(fileInfo->Key.Buffer, fileInfo->Key.Length / sizeof(WCHAR));
}

VOID EnpCreateHashtableFileInfo(
    __inout PEN_FILEINFO FileInfo
    )
{
    assert(!FileInfo->Files);
    FileInfo->Files = PhCreateHashtable(
        sizeof(PEN_FILEINFO),
        EnpFileInfoCompareFunction,
        EnpFileInfoHashFunction,
        8
        );
}

VOID EnpDestroyFileInfo(
    __in PEN_FILEINFO FileInfo
    )
{
    if (FileInfo->Files)
        EnpClearFileInfo(FileInfo);

    EnpFreeFileInfo(FileInfo);
}

VOID EnpClearFileInfo(
    __in PEN_FILEINFO FileInfo
    )
{
    PH_HASHTABLE_ENUM_CONTEXT enumContext;
    PEN_FILEINFO *fileInfo;

    PhBeginEnumHashtable(FileInfo->Files, &enumContext);

    while (fileInfo = PhNextEnumHashtable(&enumContext))
    {
        EnpDestroyFileInfo(*fileInfo);
    }

    PhClearHashtable(FileInfo->Files);
}

VOID EnpFreeFileInfo(
    __in PEN_FILEINFO FileInfo
    )
{
    PhSwapReference(&FileInfo->Name, NULL);
    PhSwapReference(&FileInfo->FullFileName, NULL);
    PhSwapReference(&FileInfo->FullSourceFileName, NULL);
    PhSwapReference(&FileInfo->Files, NULL);
    PhFree(FileInfo);
}

PEN_FILEINFO EnpFindFileInfo(
    __in PEN_FILEINFO FileInfo,
    __in PPH_STRINGREF Name
    )
{
    EN_FILEINFO lookupFileInfo;
    PEN_FILEINFO lookupFileInfoPtr = &lookupFileInfo;
    PEN_FILEINFO *fileInfo;

    if (!FileInfo->Files)
        return NULL;

    lookupFileInfo.Key = *Name;
    fileInfo = PhFindEntryHashtable(FileInfo->Files, &lookupFileInfoPtr);

    if (fileInfo)
        return *fileInfo;
    else
        return NULL;
}

PEN_FILEINFO EnpCreateRootFileInfo(
    VOID
    )
{
    PEN_FILEINFO fileInfo;

    fileInfo = EnpCreateFileInfo(NULL, NULL, NULL);
    fileInfo->Directory = TRUE;
    fileInfo->FsExpand = FALSE;

    return fileInfo;
}

VOID EnpMapBaseNamesFileInfo(
    __in PBK_CONFIG Config,
    __inout PEN_FILEINFO FileInfo
    )
{
    // Perform required mappings.
    PhSwapReference2(&FileInfo->FullSourceFileName, EnpMapFileName(
        Config,
        &FileInfo->FullSourceFileName->sr,
        FileInfo->FullSourceFileName
        ));
}

VOID EnpTrimTrailingBackslashes(
    __inout PPH_STRINGREF String
    )
{
    // Remove trailing backslashes.
    while (String->Length != 0 && String->Buffer[String->Length / sizeof(WCHAR) - 1] == '\\')
        String->Length -= sizeof(WCHAR);
}

VOID EnpAddSourceToRoot(
    __in PBK_CONFIG Config,
    __in PEN_FILEINFO Root,
    __in PPH_STRINGREF SourceFileName,
    __in BOOLEAN Directory
    )
{
    PH_STRINGREF part;
    PH_STRINGREF remainingPart;
    PH_STRINGREF name;
    PEN_FILEINFO fileInfo;
    PEN_FILEINFO newFileInfo;
    PEN_FILEINFO needMapFileInfo;

    needMapFileInfo = NULL;
    fileInfo = Root;
    remainingPart = *SourceFileName;
    EnpTrimTrailingBackslashes(&remainingPart);

    while (remainingPart.Length != 0)
    {
        PhSplitStringRefAtChar(&remainingPart, '\\', &part, &remainingPart);

        if (part.Length != 0)
        {
            name = part;

            // If this is the first part, strip the trailing colon from the name if present.
            if (fileInfo == Root && name.Length != 0 && name.Buffer[name.Length / sizeof(WCHAR) - 1] == ':')
            {
                name.Length -= sizeof(WCHAR);
            }

            if (remainingPart.Length != 0)
            {
                newFileInfo = EnpFindFileInfo(fileInfo, &name);

                if (!newFileInfo)
                {
                    newFileInfo = EnpCreateFileInfo(fileInfo, &name, &part);

                    if (fileInfo == Root)
                        EnpMapBaseNamesFileInfo(Config, newFileInfo);

                    newFileInfo->Directory = TRUE;
                    newFileInfo->FsExpand = FALSE;
                    newFileInfo->NeedFsInfo = TRUE;

                    if (!fileInfo->Files)
                        EnpCreateHashtableFileInfo(fileInfo);

                    PhAddEntryHashtable(fileInfo->Files, &newFileInfo);
                }

                fileInfo = newFileInfo;
            }
            else
            {
                newFileInfo = EnpFindFileInfo(fileInfo, &name);

                if (newFileInfo)
                {
                    newFileInfo->FsExpand = TRUE;
                    newFileInfo->NeedFsInfo = TRUE;

                    if (newFileInfo != Root && newFileInfo->Files)
                    {
                        EnpClearFileInfo(newFileInfo);
                    }
                }
                else
                {
                    newFileInfo = EnpCreateFileInfo(fileInfo, &name, &part);

                    if (Directory)
                    {
                        if (fileInfo == Root)
                            EnpMapBaseNamesFileInfo(Config, newFileInfo);

                        newFileInfo->Directory = TRUE;
                        newFileInfo->FsExpand = TRUE;
                        newFileInfo->NeedFsInfo = TRUE;
                    }
                    else
                    {
                        newFileInfo->Directory = FALSE;
                        newFileInfo->NeedFsInfo = TRUE;
                    }

                    if (!fileInfo->Files)
                        EnpCreateHashtableFileInfo(fileInfo);

                    PhAddEntryHashtable(fileInfo->Files, &newFileInfo);
                }
            }
        }
    }
}

VOID EnpPopulateRootFileInfo(
    __in PBK_CONFIG Config,
    __inout PEN_FILEINFO Root
    )
{
    ULONG i;
    PPH_STRING name;

    for (i = 0; i < Config->SourceDirectoryList->Count; i++)
    {
        name = Config->SourceDirectoryList->Items[i];

        if (EnpMatchFileName(Config, name))
            EnpAddSourceToRoot(Config, Root, &name->sr, TRUE);
    }

    for (i = 0; i < Config->SourceFileList->Count; i++)
    {
        name = Config->SourceFileList->Items[i];

        if (EnpMatchFileName(Config, name))
            EnpAddSourceToRoot(Config, Root, &name->sr, FALSE);
    }
}

BOOLEAN NTAPI EnpEnumDirectoryFile(
    __in PFILE_DIRECTORY_INFORMATION Information,
    __in_opt PVOID Context
    )
{
    PEN_POPULATE_FS_CONTEXT context = Context;
    PH_STRINGREF fileName;
    PEN_FILEINFO fileInfo;

    fileName.Buffer = Information->FileName;
    fileName.Length = Information->FileNameLength;

    if (PhEqualStringRef2(&fileName, L".", FALSE))
        return TRUE;
    if (PhEqualStringRef2(&fileName, L"..", FALSE))
        return TRUE;

    // Never follow symbolic links, mount points or other reparse points.
    if (Information->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return TRUE;

    fileInfo = EnpCreateFileInfo(context->FileInfo, &fileName, &fileName);

    if (!EnpMatchFileName(context->Config, fileInfo->FullSourceFileName) ||
        (!(Information->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !EnpMatchFileSize(context->Config, fileInfo->FullSourceFileName, &Information->EndOfFile)))
    {
        EnpDestroyFileInfo(fileInfo);
        return TRUE;
    }

    if (Information->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        fileInfo->Directory = TRUE;
        fileInfo->FsExpand = TRUE;
    }
    else
    {
        fileInfo->Directory = FALSE;
    }

    fileInfo->FileInformation.CreationTime = Information->CreationTime;
    fileInfo->FileInformation.LastAccessTime = Information->LastAccessTime;
    fileInfo->FileInformation.LastWriteTime = Information->LastWriteTime;
    fileInfo->FileInformation.ChangeTime = Information->ChangeTime;
    fileInfo->FileInformation.AllocationSize = Information->AllocationSize;
    fileInfo->FileInformation.EndOfFile = Information->EndOfFile;
    fileInfo->FileInformation.FileAttributes = Information->FileAttributes;

    PhAddEntryHashtable(context->FileInfo->Files, &fileInfo);

    return TRUE;
}

NTSTATUS EnpPopulateFsFileInfo(
    __in PBK_CONFIG Config,
    __inout PEN_FILEINFO FileInfo,
    __in_opt PBK_VSS_OBJECT Vss
    )
{
    NTSTATUS status;
    EN_POPULATE_FS_CONTEXT context;
    HANDLE fileHandle;
    PPH_STRING sourceFileName;
    WCHAR buffer[4];
    PWSTR fileName;

    context.Config = Config;
    context.FileInfo = FileInfo;

    sourceFileName = FileInfo->FullSourceFileName;
    PhReferenceObject(sourceFileName);

    if (Vss)
    {
        PhSwapReference2(&sourceFileName, BkMapFileNameVssObject(Vss, sourceFileName));
    }

    fileName = sourceFileName->Buffer;

    // Drive names need a backslash after them.
    if (sourceFileName->Length == 2 * sizeof(WCHAR) && sourceFileName->Buffer[1] == ':')
    {
        buffer[0] = sourceFileName->Buffer[0];
        buffer[1] = ':';
        buffer[2] = '\\';
        buffer[3] = 0;
        fileName = buffer;
    }

    status = PhCreateFileWin32(
        &fileHandle,
        fileName,
        FILE_LIST_DIRECTORY | SYNCHRONIZE,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT
        );

    if (!NT_SUCCESS(status))
    {
        status = PhCreateFileWin32(
            &fileHandle,
            fileName,
            FILE_LIST_DIRECTORY | SYNCHRONIZE,
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_OPEN,
            FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
            );
    }

    PhDereferenceObject(sourceFileName);

    if (!NT_SUCCESS(status))
        return status;

    if (!FileInfo->Files)
        EnpCreateHashtableFileInfo(FileInfo);

    PhEnumDirectoryFile(fileHandle, NULL, EnpEnumDirectoryFile, &context);
    NtClose(fileHandle);

    return STATUS_SUCCESS;
}

NTSTATUS EnpUpdateFsFileInfo(
    __inout PEN_FILEINFO FileInfo,
    __in_opt PBK_VSS_OBJECT Vss
    )
{
    NTSTATUS status;
    PPH_STRING fileName;

    if (Vss)
    {
        fileName = BkMapFileNameVssObject(Vss, FileInfo->FullSourceFileName);
    }
    else
    {
        fileName = FileInfo->FullSourceFileName;
        PhReferenceObject(fileName);
    }

    status = EnpQueryFullAttributesFileWin32(fileName->Buffer, &FileInfo->FileInformation);
    PhDereferenceObject(fileName);

    return status;
}

VOID EnpSetDiffFlagsFileInfo(
    __inout PEN_FILEINFO FileInfo,
    __in UCHAR DiffFlags
    )
{
    PH_HASHTABLE_ENUM_CONTEXT enumContext;
    PEN_FILEINFO *fileInfoPtr;

    FileInfo->DiffFlags = DiffFlags;

    if (FileInfo->Files)
    {
        PhBeginEnumHashtable(FileInfo->Files, &enumContext);

        while (fileInfoPtr = PhNextEnumHashtable(&enumContext))
            EnpSetDiffFlagsFileInfo(*fileInfoPtr, DiffFlags);
    }
}

NTSTATUS EnpCreateTransaction(
    __out PHANDLE TransactionHandle,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;

    status = NtCreateTransaction(
        TransactionHandle,
        TRANSACTION_ALL_ACCESS,
        NULL,
        NULL,
        NULL,
        0,
        0,
        0,
        NULL,
        NULL
        );

    if (!NT_SUCCESS(status))
    {
        if (MessageHandler)
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to create transaction: 0x%x", status));
    }

    return status;
}

NTSTATUS EnpCommitAndCloseTransaction(
    __in NTSTATUS CurrentStatus,
    __in_opt HANDLE TransactionHandle,
    __in BOOLEAN TrivialCommit,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
    )
{
    NTSTATUS status;

    if (TransactionHandle)
    {
        if (NT_SUCCESS(CurrentStatus))
        {
            if (!TrivialCommit)
            {
                if (MessageHandler)
                    MessageHandler(EN_MESSAGE_PROGRESS, PhCreateString(L"Committing transaction"));

                status = NtCommitTransaction(TransactionHandle, TRUE);
            }
            else
            {
                status = NtCommitTransaction(TransactionHandle, FALSE);
            }
        }
        else
        {
            status = NtRollbackTransaction(TransactionHandle, FALSE);
        }

        NtClose(TransactionHandle);

        if (!NT_SUCCESS(status) && MessageHandler)
            MessageHandler(EN_MESSAGE_ERROR, PhFormatString(L"Unable to process transaction: 0x%x", status));

        if (!NT_SUCCESS(CurrentStatus))
            status = CurrentStatus;

        RtlSetCurrentTransaction(NULL);
    }
    else
    {
        status = CurrentStatus;
    }

    return status;
}

VOID EnpStartVssObject(
    __in PBK_CONFIG Config,
    __in PEN_FILEINFO Root,
    __out PBK_VSS_OBJECT *Vss,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    HRESULT result;
    PBK_VSS_OBJECT vss;

    vss = NULL;

    if (SUCCEEDED(result = BkCreateVssObject(&vss)))
    {
        if (SUCCEEDED(result = BkStartSnapshotsVssObject(vss)))
        {
            EnpConfigureVssObject(vss, Root, MessageHandler);

            if (!SUCCEEDED(result = BkPerformSnapshotsVssObject(vss)))
            {
                MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Failed to perform VSS snapshots: 0x%x", result));
            }
        }
        else
        {
            MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Failed to start VSS snapshot set: 0x%x", result));
        }
    }
    else
    {
        MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Failed to connect to VSS: 0x%x", result));
    }

    *Vss = vss;
}

VOID EnpConfigureVssObject(
    __in PBK_VSS_OBJECT Vss,
    __in PEN_FILEINFO Root,
    __in PEN_MESSAGE_HANDLER MessageHandler
    )
{
    HRESULT result;
    PH_HASHTABLE_ENUM_CONTEXT enumContext;
    PEN_FILEINFO *filePtr;
    PEN_FILEINFO file;
    BOOLEAN added[26];
    WCHAR buffer[4];
    PPH_STRING volume;

    if (!Root->Files)
        return;

    memset(added, 0, sizeof(added));
    PhBeginEnumHashtable(Root->Files, &enumContext);

    while (filePtr = PhNextEnumHashtable(&enumContext))
    {
        file = *filePtr;

        if (file->FullSourceFileName->Length >= 2 * sizeof(WCHAR) && file->FullSourceFileName->Buffer[1] == ':')
        {
            WCHAR letter;

            letter = file->FullSourceFileName->Buffer[0];

            if (letter >= 'a' && letter <= 'z')
            {
                letter = letter - ('a' - 'A');
            }
            else if (!(letter >= 'A' && letter <= 'Z'))
            {
                continue;
            }

            if (added[letter - 'A'])
                continue;

            buffer[0] = letter;
            buffer[1] = ':';
            buffer[2] = '\\';
            buffer[3] = 0;
            volume = PhCreateStringEx(buffer, 3 * sizeof(WCHAR));

            if (!SUCCEEDED(result = BkAddSnapshotVssObject(Vss, volume)))
            {
                MessageHandler(EN_MESSAGE_WARNING, PhFormatString(L"Failed to add VSS snapshot for %c: 0x%x", letter, result));
            }

            added[letter - 'A'] = TRUE; // if we failed, there's no point in trying again
        }
    }
}

BOOLEAN EnpMatchFileName(
    __in PBK_CONFIG Config,
    __in PPH_STRING FileName
    )
{
    ULONG i;
    PPH_STRING filter;
    BOOLEAN include;

    if (Config->IncludeList->Count != 0)
    {
        include = FALSE;

        for (i = 0; i < Config->IncludeList->Count; i++)
        {
            filter = Config->IncludeList->Items[i];

            if (PhMatchWildcards(filter->Buffer, FileName->Buffer, TRUE))
            {
                include = TRUE;
                break;
            }
        }

        if (!include)
            return FALSE;
    }

    for (i = 0; i < Config->ExcludeList->Count; i++)
    {
        filter = Config->ExcludeList->Items[i];

        if (PhMatchWildcards(filter->Buffer, FileName->Buffer, TRUE))
            return FALSE;
    }

    return TRUE;
}

BOOLEAN EnpMatchFileSize(
    __in PBK_CONFIG Config,
    __in PPH_STRING FileName,
    __in PLARGE_INTEGER Size
    )
{
    ULONG i;
    PPH_STRING filter;
    BOOLEAN include;

    if (Config->IncludeSizeList->Count != 0)
    {
        include = FALSE;

        for (i = 0; i < Config->IncludeSizeList->Count; i++)
        {
            filter = Config->IncludeSizeList->Items[i];

            if (EnpMatchFileSizeAgainstExpression(filter, FileName, Size))
            {
                include = TRUE;
                break;
            }
        }

        if (!include)
            return FALSE;
    }

    for (i = 0; i < Config->ExcludeSizeList->Count; i++)
    {
        filter = Config->ExcludeSizeList->Items[i];

        if (EnpMatchFileSizeAgainstExpression(filter, FileName, Size))
            return FALSE;
    }

    return TRUE;
}

BOOLEAN EnpMatchFileSizeAgainstExpression(
    __in PPH_STRING String,
    __in PPH_STRING FileName,
    __in PLARGE_INTEGER Size
    )
{
    PH_STRINGREF filePart;
    PH_STRINGREF string;
    ULONG64 integer;

    if (String->Length == 0)
        return FALSE;

    if (!PhSplitStringRefAtLastChar(&String->sr, '|', &filePart, &string))
    {
        PhInitializeEmptyStringRef(&filePart);
        string = String->sr;
    }

    if (filePart.Length != 0)
    {
        PPH_STRING filePartString;

        filePartString = PhCreateStringEx(filePart.Buffer, filePart.Length);

        if (!PhMatchWildcards(filePartString->Buffer, FileName->Buffer, TRUE))
        {
            PhDereferenceObject(filePartString);
            return FALSE;
        }

        PhDereferenceObject(filePartString);
    }

    if (string.Buffer[0] == '>')
    {
        string.Buffer++;
        string.Length -= sizeof(WCHAR);
        integer = EnpStringToSize(&string);

        if ((ULONG64)Size->QuadPart > integer)
            return TRUE;
    }
    else if (string.Buffer[0] == '<')
    {
        string.Buffer++;
        string.Length -= sizeof(WCHAR);
        integer = EnpStringToSize(&string);

        if ((ULONG64)Size->QuadPart < integer)
            return TRUE;
    }

    return FALSE;
}

ULONG64 EnpStringToSize(
    __in PPH_STRINGREF String
    )
{
    PH_STRINGREF string;
    ULONG64 integer;
    ULONG64 multiplier;

    string = *String;
    multiplier = 1;

    if (PhEndsWithStringRef2(&string, L"KB", TRUE))
    {
        string.Length -= 2 * sizeof(WCHAR);
        multiplier = 1024;
    }
    else if (PhEndsWithStringRef2(&string, L"MB", TRUE))
    {
        string.Length -= 2 * sizeof(WCHAR);
        multiplier = 1024 * 1024;
    }
    else if (PhEndsWithStringRef2(&string, L"GB", TRUE))
    {
        string.Length -= 2 * sizeof(WCHAR);
        multiplier = 1024 * 1024 * 1024;
    }
    else if (string.Length >= 2 * sizeof(WCHAR) &&
        PhEndsWithStringRef2(&string, L"B", TRUE) &&
        PhIsDigitCharacter(string.Buffer[string.Length / sizeof(WCHAR) - 2]))
    {
        string.Length -= sizeof(WCHAR);
    }
    else
    {
        return 0;
    }

    PhStringToInteger64(&string, 10, &integer);
    integer *= multiplier;

    return integer;
}

PPH_STRING EnpMapFileName(
    __in PBK_CONFIG Config,
    __in_opt PPH_STRINGREF FileName,
    __in_opt PPH_STRING FileNameString
    )
{
    ULONG i;
    PPH_STRING prefix;
    PPH_STRING newPrefix;

    if (!FileName && !FileNameString)
        PhRaiseStatus(STATUS_INVALID_PARAMETER_MIX);

    if (!FileName)
        FileName = &FileNameString->sr;

    for (i = 0; i < Config->MapFromList->Count; i++)
    {
        prefix = Config->MapFromList->Items[i];

        if (PhStartsWithStringRef(FileName, &prefix->sr, TRUE))
        {
            PPH_STRING newString;

            newPrefix = Config->MapToList->Items[i];
            newString = PhCreateStringEx(NULL, FileName->Length - prefix->Length + newPrefix->Length);
            memcpy(newString->Buffer, newPrefix->Buffer, newPrefix->Length);
            memcpy((PCHAR)newString->Buffer + newPrefix->Length, (PCHAR)FileName->Buffer + prefix->Length, FileName->Length - prefix->Length);

            return newString;
        }
    }

    if (FileNameString)
        PhReferenceObject(FileNameString);
    else
        FileNameString = PhCreateStringEx(FileName->Buffer, FileName->Length);

    return FileNameString;
}

PPH_STRING EnpAppendComponentToPath(
    __in PPH_STRINGREF FileName,
    __in PPH_STRINGREF Component
    )
{
    PH_STRINGREF fileName;
    PH_STRINGREF component;

    fileName = *FileName;
    component = *Component;

    EnpTrimTrailingBackslashes(&fileName);

    while (component.Length != 0 && component.Buffer[0] == '\\')
    {
        component.Buffer++;
        component.Length -= sizeof(WCHAR);
    }

    if (fileName.Length == 0)
        return PhCreateStringEx(component.Buffer, component.Length);
    if (component.Length == 0)
        return PhCreateStringEx(fileName.Buffer, fileName.Length);

    return PhConcatStringRef3(&fileName, &EnpBackslashString, &component);
}

NTSTATUS EnpQueryFullAttributesFileWin32(
    __in PWSTR FileName,
    __out PFILE_NETWORK_OPEN_INFORMATION FileInformation
    )
{
    NTSTATUS status;
    UNICODE_STRING fileName;
    OBJECT_ATTRIBUTES oa;

    if (!RtlDosPathNameToNtPathName_U(
        FileName,
        &fileName,
        NULL,
        NULL
        ))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    InitializeObjectAttributes(
        &oa,
        &fileName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
        );

    status = NtQueryFullAttributesFile(&oa, FileInformation);
    RtlFreeHeap(RtlProcessHeap(), 0, fileName.Buffer);

    return status;
}

NTSTATUS EnpRenameFileWin32(
    __in_opt HANDLE FileHandle,
    __in_opt PWSTR FileName,
    __in PWSTR NewFileName
    )
{
    NTSTATUS status;
    HANDLE fileHandle;
    UNICODE_STRING newFileNameNt;
    ULONG renameInfoSize;
    PFILE_RENAME_INFORMATION renameInfo;
    IO_STATUS_BLOCK iosb;

    if (!FileHandle)
    {
        status = PhCreateFileWin32(
            &fileHandle,
            FileName,
            DELETE | SYNCHRONIZE,
            0,
            0,
            FILE_OPEN,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
            );
    }
    else
    {
        fileHandle = FileHandle;
        status = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(status))
    {
        status = RtlDosPathNameToNtPathName_U(
            NewFileName,
            &newFileNameNt,
            NULL,
            NULL
            );

        if (NT_SUCCESS(status))
        {
            renameInfoSize = FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + newFileNameNt.Length;
            renameInfo = PhAllocate(renameInfoSize);
            renameInfo->ReplaceIfExists = FALSE;
            renameInfo->RootDirectory = NULL;
            renameInfo->FileNameLength = newFileNameNt.Length;
            memcpy(renameInfo->FileName, newFileNameNt.Buffer, newFileNameNt.Length);

            status = NtSetInformationFile(fileHandle, &iosb, renameInfo, (ULONG)renameInfoSize, FileRenameInformation);
            PhFree(renameInfo);
            RtlFreeHeap(RtlProcessHeap(), 0, newFileNameNt.Buffer);
        }

        if (fileHandle != FileHandle)
            NtClose(fileHandle);
    }

    return status;
}

NTSTATUS EnpCopyFileWin32(
    __in PWSTR FileName,
    __in PWSTR NewFileName,
    __in ULONG NewFileAttributes,
    __in BOOLEAN OverwriteIfExists
    )
{
    NTSTATUS status;
    HANDLE fileHandle;
    HANDLE outFileHandle;
    LARGE_INTEGER fileSize;
    IO_STATUS_BLOCK iosb;
    PVOID buffer;
    ULONG bufferSize;
    CHAR stackBuffer[PAGE_SIZE];
    ULONGLONG bytesToCopy;
    FILE_BASIC_INFORMATION basicInfo;
    FILE_DISPOSITION_INFORMATION dispositionInfo;

    status = PhCreateFileWin32(
        &fileHandle,
        FileName,
        FILE_READ_ATTRIBUTES | FILE_READ_DATA | SYNCHRONIZE,
        0,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SEQUENTIAL_ONLY | FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (!NT_SUCCESS(status))
        return status;

    status = PhCreateFileWin32(
        &outFileHandle,
        NewFileName,
        FILE_WRITE_ATTRIBUTES | FILE_WRITE_DATA | DELETE | SYNCHRONIZE,
        NewFileAttributes,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        !OverwriteIfExists ? FILE_CREATE : FILE_OVERWRITE_IF,
        FILE_NON_DIRECTORY_FILE | FILE_SEQUENTIAL_ONLY | FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (!NT_SUCCESS(status))
    {
        NtClose(fileHandle);
        return status;
    }

    if (NT_SUCCESS(PhGetFileSize(fileHandle, &fileSize)))
    {
        bytesToCopy = fileSize.QuadPart;
        PhSetFileSize(outFileHandle, &fileSize);
    }

    bufferSize = 0x10000;
    buffer = PhAllocatePage(bufferSize, NULL);

    if (!buffer)
    {
        buffer = stackBuffer;
        bufferSize = sizeof(stackBuffer);
    }

    while (bytesToCopy != 0)
    {
        status = NtReadFile(
            fileHandle,
            NULL,
            NULL,
            NULL,
            &iosb,
            buffer,
            bytesToCopy >= bufferSize ? bufferSize : (ULONG)bytesToCopy,
            NULL,
            NULL
            );

        if (status == STATUS_PENDING)
        {
            NtWaitForSingleObject(fileHandle, FALSE, NULL);
            status = iosb.Status;
        }

        if (status == STATUS_END_OF_FILE)
        {
            status = STATUS_SUCCESS;
            break;
        }
        else if (!NT_SUCCESS(status))
        {
            dispositionInfo.DeleteFile = TRUE;
            NtSetInformationFile(outFileHandle, &iosb, &dispositionInfo, sizeof(FILE_DISPOSITION_INFORMATION), FileDispositionInformation);
            break;
        }

        status = NtWriteFile(
            outFileHandle,
            NULL,
            NULL,
            NULL,
            &iosb,
            buffer,
            (ULONG)iosb.Information, // number of bytes read
            NULL,
            NULL
            );

        if (status == STATUS_PENDING)
        {
            NtWaitForSingleObject(fileHandle, FALSE, NULL);
            status = iosb.Status;
        }

        if (!NT_SUCCESS(status))
        {
            dispositionInfo.DeleteFile = TRUE;
            NtSetInformationFile(outFileHandle, &iosb, &dispositionInfo, sizeof(FILE_DISPOSITION_INFORMATION), FileDispositionInformation);
            break;
        }

        bytesToCopy -= (ULONG)iosb.Information;
    }

    if (buffer)
        PhFreePage(buffer);

    // Copy basic attributes over.
    if (NT_SUCCESS(NtQueryInformationFile(
        fileHandle,
        &iosb,
        &basicInfo,
        sizeof(FILE_BASIC_INFORMATION),
        FileBasicInformation
        )))
    {
        NtSetInformationFile(
            outFileHandle,
            &iosb,
            &basicInfo,
            sizeof(FILE_BASIC_INFORMATION),
            FileBasicInformation
            );
    }

    NtClose(fileHandle);
    NtClose(outFileHandle);

    return status;
}

VOID EnpFormatRevisionId(
    __in ULONGLONG RevisionId,
    __out_ecount(17) PWSTR String
    )
{
    PH_FORMAT format;

    PhInitFormatI64U(&format, RevisionId);
    format.Type |= FormatUseRadix | FormatPadZeros;
    format.Width = 16;
    format.Radix = 16;

    PhFormatToBuffer(&format, 1, String, 17 * sizeof(WCHAR), NULL);
}

PPH_STRING EnpFormatPackageName(
    __in PBK_CONFIG Config,
    __in ULONGLONG RevisionId
    )
{
    PH_FORMAT format[4];

    PhInitFormatSR(&format[0], Config->DestinationDirectory->sr);
    PhInitFormatC(&format[1], '\\');
    PhInitFormatI64U(&format[2], RevisionId);
    format[2].Type |= FormatUseRadix | FormatPadZeros;
    format[2].Width = 16;
    format[2].Radix = 16;
    PhInitFormatS(&format[3], L".7z");

    return PhFormat(format, 4, Config->DestinationDirectory->Length + 20 * sizeof(WCHAR));
}

PPH_STRING EnpFormatTempDatabaseFileName(
    __in PBK_CONFIG Config,
    __in BOOLEAN SameDirectory
    )
{
    WCHAR tempNameBuffer[18];
    PH_STRINGREF tempNameSr;
    WCHAR tempPathBuffer[MAX_PATH + 1];
    PH_STRINGREF tempPathSr;

    tempNameBuffer[0] = 'd';
    tempNameBuffer[1] = 'b';
    tempNameBuffer[2] = '.';
    tempNameBuffer[3] = 'b';
    tempNameBuffer[4] = 'k';
    tempNameBuffer[5] = '.';
    PhGenerateRandomAlphaString(tempNameBuffer + 6, 9);
    tempNameBuffer[14] = '.';
    tempNameBuffer[15] = 't';
    tempNameBuffer[16] = 'm';
    tempNameBuffer[17] = 'p';
    tempNameSr.Buffer = tempNameBuffer;
    tempNameSr.Length = 18 * sizeof(WCHAR);

    if (!SameDirectory && GetTempPath(MAX_PATH + 1, tempPathBuffer) != 0)
    {
        PhInitializeStringRef(&tempPathSr, tempPathBuffer);

        return PhConcatStringRef2(&tempPathSr, &tempNameSr);
    }
    else
    {
        return EnpAppendComponentToPath(&Config->DestinationDirectory->sr, &tempNameSr);
    }
}

NTSTATUS EnpCreateDatabase(
    __in PBK_CONFIG Config
    )
{
    NTSTATUS status;
    PPH_STRING databaseFileName;
    PH_STRINGREF name;

    PhInitializeStringRef(&name, EN_DATABASE_NAME);
    databaseFileName = EnpAppendComponentToPath(&Config->DestinationDirectory->sr, &name);

    status = DbCreateDatabase(databaseFileName->Buffer);
    PhDereferenceObject(databaseFileName);

    return status;
}

NTSTATUS EnpOpenDatabase(
    __in PBK_CONFIG Config,
    __in BOOLEAN ReadOnly,
    __out PDB_DATABASE *Database
    )
{
    NTSTATUS status;
    PPH_STRING databaseFileName;
    PH_STRINGREF name;

    PhInitializeStringRef(&name, EN_DATABASE_NAME);
    databaseFileName = EnpAppendComponentToPath(&Config->DestinationDirectory->sr, &name);

    status = DbOpenDatabase(Database, databaseFileName->Buffer, ReadOnly, FILE_SHARE_READ);
    PhDereferenceObject(databaseFileName);

    return status;
}

VOID EnpDefaultMessageHandler(
    __in ULONG Level,
    __in __assumeRefs(1) PPH_STRING Message
    )
{
    PhDereferenceObject(Message);
}
