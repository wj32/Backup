/*
 * Backup -
 *   command line interface
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
#include "engine.h"
#include "db.h"
#include "cmdlinep.h"
#include <shlobj.h>

static BOOLEAN ShowHelp;
static PPH_STRING ConfigFileName;
static ULONGLONG ParameterRevisionId;
static ULONGLONG ParameterRevisionId2;
static BOOLEAN ParameterForce;
static ULONGLONG ParameterTime;

static PPH_STRING Command;
static PPH_STRING CommandParameter;
static PPH_STRING CommandParameter2;

static BOOLEAN LastLineWasCr;

LONG BkRunCommandLine(
    VOID
    )
{
    static PH_COMMAND_LINE_OPTION options[] =
    {
        { OPTION_CONFIGFILENAME, L"c", MandatoryArgumentType },
        { OPTION_HELP, L"h", NoArgumentType },
        { OPTION_HELP, L"?", NoArgumentType },
        { OPTION_HELP, L"-help", NoArgumentType },
        { OPTION_REVISIONID, L"r", MandatoryArgumentType },
        { OPTION_FORCE, L"-force", NoArgumentType },
        { OPTION_TIME, L"t", MandatoryArgumentType }
    };

    NTSTATUS status;
    PH_STRINGREF commandLine;
    PBK_CONFIG config;

    PhUnicodeStringToStringRef(&NtCurrentPeb()->ProcessParameters->CommandLine, &commandLine);

    if (!PhParseCommandLine(
        &commandLine,
        options,
        sizeof(options) / sizeof(PH_COMMAND_LINE_OPTION),
        PH_COMMAND_LINE_IGNORE_FIRST_PART,
        CommandLineCallback,
        NULL
        ) || ShowHelp || !Command)
    {
        PrintHelp(Command);
        return 1;
    }

    if (!ConfigFileName)
        ConfigFileName = PhCreateString(L"config.ini");
    //if (!Command)
    //    Command = PhCreateString(L"status");

    if (!NT_SUCCESS(BkCreateConfigFromFile(ConfigFileName->Buffer, &config)))
    {
        wprintf(L"Can't read configuration file from %s\n", ConfigFileName->Buffer);
        wprintf(L"Use 'bkc --help config' to show help.\n");
        return 1;
    }

    if (!config->DestinationDirectory)
    {
        wprintf(L"== Error: DestinationDirectory not specified in configuration file.\n");
        return 1;
    }

    if (PhEqualString2(Command, L"status", TRUE))
    {
        ULONGLONG revisionId;
        LARGE_INTEGER revisionTimeStamp;
        ULONGLONG firstRevisionId;
        LARGE_INTEGER firstRevisionTimeStamp;

        if (!RtlDoesFileExists_U(PhConcatStrings2(config->DestinationDirectory->Buffer, L"\\" EN_DATABASE_NAME)->Buffer))
        {
            wprintf(L"== Database not present. Use 'bkc backup' to create the first revision.\n");
            return 0;
        }

        status = EnQueryRevision(config, ConsoleMessageHandler, &revisionId, &revisionTimeStamp, &firstRevisionId, &firstRevisionTimeStamp);
        RecoverAfterEngineMessages();

        if (NT_SUCCESS(status))
        {
            wprintf(L"== Location: %s\n", config->DestinationDirectory->Buffer);
            wprintf(L"== Range: %I64u (%s) .. %I64u (%s).\n",
                firstRevisionId, FormatUtcTime(&firstRevisionTimeStamp)->Buffer, revisionId, FormatUtcTime(&revisionTimeStamp)->Buffer);
            wprintf(L"== At revision %I64u.\n", revisionId);
        }
        else
        {
            wprintf(L"== Error: 0x%x\n          %s\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
            return 1;
        }
    }
    else if (PhEqualString2(Command, L"diff", TRUE))
    {
        if (ParameterRevisionId != 0)
        {
            status = EnCompareRevisions(config, ParameterRevisionId, ParameterRevisionId2, ConsoleMessageHandler);
        }
        else
        {
            status = EnTestBackupToRevision(config, ConsoleMessageHandler);
        }

        RecoverAfterEngineMessages();

        if (!NT_SUCCESS(status))
        {
            wprintf(L"== Error: 0x%x\n          %s\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
            return 1;
        }
    }
    else if (PhEqualString2(Command, L"log", TRUE))
    {
        PEN_FILE_REVISION_INFORMATION entries;
        ULONG numberOfEntries;
        ULONG i;
        PPH_STRING sizeString;
        PPH_STRING timeStampString;
        PPH_STRING lastBackupTimeString;

        if (!CommandParameter)
            CommandParameter = PhReferenceEmptyString();

        status = EnQueryFileRevisions(config, &CommandParameter->sr, ConsoleMessageHandler, &entries, &numberOfEntries);

        if (NT_SUCCESS(status))
        {
            for (i = 0; i < numberOfEntries; i++)
            {
                sizeString = PhFormatUInt64(entries[i].EndOfFile.QuadPart, TRUE);

                timeStampString = FormatUtcTime(&entries[i].TimeStamp);

                if (entries[i].LastBackupTime.QuadPart != 0)
                    lastBackupTimeString = FormatUtcTime(&entries[i].LastBackupTime);
                else
                    lastBackupTimeString = PhReferenceEmptyString();

                wprintf(L"%I64u\t%20s\t%20s\t%10s\n", entries[i].RevisionId, timeStampString->Buffer, lastBackupTimeString->Buffer, sizeString->Buffer);

                PhDereferenceObject(lastBackupTimeString);
                PhDereferenceObject(timeStampString);
                PhDereferenceObject(sizeString);
            }

            PhFree(entries);
        }
        else
        {
            wprintf(L"== Error: 0x%x\n          %s\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
            return 1;
        }
    }
    else if (PhEqualString2(Command, L"backup", TRUE))
    {
        ULONGLONG revisionId;

        status = EnBackupToRevision(config, ConsoleMessageHandler, &revisionId);
        RecoverAfterEngineMessages();

        if (NT_SUCCESS(status))
        {
            wprintf(L"== At revision %I64u.\n", revisionId);
        }
        else
        {
            wprintf(L"== Error: 0x%x\n          %s\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
            return 1;
        }
    }
    else if (PhEqualString2(Command, L"revert", TRUE))
    {
        ULONGLONG revisionId;
        //ULONGLONG firstRevisionId;
        ULONGLONG targetRevisionId;

        if (ParameterRevisionId == 0)
        {
            //status = EnQueryRevision(config, ConsoleMessageHandler, &revisionId, NULL, &firstRevisionId, NULL);

            //if (NT_SUCCESS(status))
            //{
            //    if (firstRevisionId == revisionId)
            //    {
            //        wprintf(L"== Error: No revisions to revert.\n");
            //        return 1;
            //    }

            //    targetRevisionId = revisionId - 1;
            //}

            wprintf(L"== Error: no revision specified (use '-r')\n");
            wprintf(L"== Use 'bkc log' to see a list of revisions or use 'bkc --help revert'.\n");
            return 1;
        }
        else
        {
            status = STATUS_SUCCESS;
            targetRevisionId = ParameterRevisionId;
        }

        if (NT_SUCCESS(status))
        {
            status = EnRevertToRevision(config, targetRevisionId, ConsoleMessageHandler, &revisionId);
            RecoverAfterEngineMessages();
        }

        if (NT_SUCCESS(status))
        {
            wprintf(L"== At revision %I64u.\n", revisionId);
        }
        else
        {
            wprintf(L"== Error: 0x%x\n          %s\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
            return 1;
        }
    }
    else if (PhEqualString2(Command, L"trim", TRUE))
    {
        //ULONGLONG revisionId;
        ULONGLONG firstRevisionId;
        ULONGLONG targetFirstRevisionId;

        if (ParameterTime != 0)
        {
            PH_STRINGREF empty;
            LARGE_INTEGER systemTime;
            PEN_FILE_REVISION_INFORMATION entries;
            ULONG numberOfEntries;
            ULONG i;
            ULONGLONG minimumRevisionId;

            PhQuerySystemTime(&systemTime);

            PhInitializeEmptyStringRef(&empty);
            status = EnQueryFileRevisions(config, &empty, ConsoleMessageHandler, &entries, &numberOfEntries);

            if (NT_SUCCESS(status))
            {
                if (numberOfEntries == 0)
                    return 0;

                minimumRevisionId = -1;

                // Find the first revision that lies within the specified time span.
                for (i = 0; i < numberOfEntries; i++)
                {
                    if ((ULONGLONG)entries[i].TimeStamp.QuadPart + ParameterTime >= (ULONGLONG)systemTime.QuadPart)
                    {
                        if (minimumRevisionId > entries[i].RevisionId)
                            minimumRevisionId = entries[i].RevisionId;
                    }
                }

                PhFree(entries);

                status = EnTrimToRevision(config, minimumRevisionId, ConsoleMessageHandler, &firstRevisionId);
            }

            if (NT_SUCCESS(status))
            {
                wprintf(L"== First revision is %I64u.\n", firstRevisionId);
            }
            else
            {
                wprintf(L"== Error: 0x%x\n          %s\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
                return 1;
            }
        }
        else
        {
            if (ParameterRevisionId == 0)
            {
                //status = EnQueryRevision(config, ConsoleMessageHandler, &revisionId, NULL, &firstRevisionId, NULL);

                //if (NT_SUCCESS(status))
                //{
                //    if (firstRevisionId == revisionId)
                //    {
                //        wprintf(L"== Error: No revisions to trim.\n");
                //        return 1;
                //    }

                //    targetFirstRevisionId = firstRevisionId + 1;
                //}

                wprintf(L"== Error: no revision specified (use '-r')\n");
                wprintf(L"== Use 'bkc log' to see a list of revisions or use 'bkc --help trim'.\n");
                return 1;
            }
            else
            {
                status = STATUS_SUCCESS;
                targetFirstRevisionId = ParameterRevisionId;
            }

            if (NT_SUCCESS(status))
            {
                status = EnTrimToRevision(config, targetFirstRevisionId, ConsoleMessageHandler, &firstRevisionId);
                RecoverAfterEngineMessages();
            }

            if (NT_SUCCESS(status))
            {
                wprintf(L"== First revision is %I64u.\n", firstRevisionId);
            }
            else
            {
                wprintf(L"== Error: 0x%x\n          %s\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
                return 1;
            }
        }
    }
    else if (PhEqualString2(Command, L"restore", TRUE))
    {
        PUNICODE_STRING currentDirectory;
        ULONG flags;
        PPH_STRINGREF restoreToDirectory;
        PH_STRINGREF localRestoreToName;
        PPH_STRINGREF restoreToName;

        if (!CommandParameter)
        {
            PrintHelp(Command);
            return 1;
        }

        // Check for a drive letter plus a colon and fix it up.
        if (CommandParameter->Length >= 2 * sizeof(WCHAR) &&
            iswalpha(CommandParameter->Buffer[0]) &&
            CommandParameter->Buffer[1] == ':')
        {
            wprintf(L"== Warning: Fixed up invalid database file name: %s\n", CommandParameter->Buffer);
            wprintf(L"== Did you mean '%c%s'?\n", CommandParameter->Buffer[0], CommandParameter->Buffer + 2);
            CommandParameter = PhFormatString(L"%c%s", CommandParameter->Buffer[0], CommandParameter->Buffer + 2);
        }

        // Check for multiple backslashes and fix them up.
        CommandParameter = FixMultipleBackslashes(&CommandParameter->sr);

        restoreToDirectory = NULL;
        restoreToName = NULL;

        if (CommandParameter2)
        {
            ULONG fileAttributes;
            PH_STRINGREF path;
            PH_STRINGREF name;
            PPH_STRING restoreToDirectoryPath;
            PPH_STRING newRestoreToDirectoryPath;

            restoreToDirectoryPath = PhGetFullPath(CommandParameter2->Buffer, NULL);

            if (restoreToDirectoryPath)
            {
                fileAttributes = GetFileAttributes(restoreToDirectoryPath->Buffer);
            }
            else
            {
                fileAttributes = INVALID_FILE_ATTRIBUTES;
            }

            if (fileAttributes != INVALID_FILE_ATTRIBUTES && (fileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                // The given path is an existing directory.
                restoreToDirectory = &restoreToDirectoryPath->sr;
            }
            else
            {
                if (restoreToDirectoryPath)
                    PhDereferenceObject(restoreToDirectoryPath);

                // If the given path has a trailing backslash, it unambiguously identifies a directory.
                if (!PhSplitStringRefAtLastChar(&CommandParameter2->sr, '\\', &path, &name))
                {
                    PhInitializeEmptyStringRef(&path);
                    name = CommandParameter2->sr;
                }

                if (path.Length == 0)
                    PhUnicodeStringToStringRef(&NtCurrentPeb()->ProcessParameters->CurrentDirectory.DosPath, &path);

                restoreToDirectoryPath = PhCreateStringEx(path.Buffer, path.Length);
                newRestoreToDirectoryPath = PhGetFullPath(restoreToDirectoryPath->Buffer, NULL);
                PhDereferenceObject(restoreToDirectoryPath);

                if (!newRestoreToDirectoryPath)
                {
                    wprintf(L"== Error: Invalid path specified\n");
                    return 1;
                }

                // Make sure the path exists.
                SHCreateDirectory(NULL, newRestoreToDirectoryPath->Buffer);
                restoreToDirectory = &newRestoreToDirectoryPath->sr;

                // If the given path didn't have a trailing backslash, we have a file name specified.
                if (name.Length != 0)
                {
                    localRestoreToName = name;
                    restoreToName = &localRestoreToName;
                }
            }
        }
        else
        {
            currentDirectory = &NtCurrentPeb()->ProcessParameters->CurrentDirectory.DosPath;
            CommandParameter2 = PhCreateStringEx(currentDirectory->Buffer, currentDirectory->Length);
            restoreToDirectory = &CommandParameter2->sr;
        }

        flags = 0;

        if (ParameterForce)
            flags |= EN_RESTORE_OVERWRITE_FILES;

        status = EnRestoreFromRevision(
            config,
            flags,
            &CommandParameter->sr,
            ParameterRevisionId,
            restoreToDirectory,
            restoreToName,
            ConsoleMessageHandler
            );
        RecoverAfterEngineMessages();

        if (!NT_SUCCESS(status))
        {
            wprintf(L"== Error: 0x%x\n          %s\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
            return 1;
        }
    }
    else if (PhEqualString2(Command, L"list", TRUE))
    {
        PPH_STRING databaseFileName;
        PDB_DATABASE db;
        PDBF_FILE rootDirectory;
        PH_STRINGREF empty;

        databaseFileName = PhConcatStrings2(config->DestinationDirectory->Buffer, L"\\" EN_DATABASE_NAME);

        status = DbOpenDatabase(&db, databaseFileName->Buffer, TRUE, FILE_SHARE_READ);

        if (!NT_SUCCESS(status))
        {
            wprintf(L"Can't open database: 0x%x (%s)\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
            return 1;
        }

        PhInitializeEmptyStringRef(&empty);

        if (NT_SUCCESS(status = DbCreateFile(db, &empty, NULL, 0, 0, 0, NULL, &rootDirectory)))
        {
            EnumDb(db, rootDirectory, NULL);
        }

        DbCloseDatabase(db);
    }
    else if (PhEqualString2(Command, L"compact", TRUE))
    {
        PPH_STRING databaseFileName;
        FILE_NETWORK_OPEN_INFORMATION oldInformation = { 0 };
        FILE_NETWORK_OPEN_INFORMATION newInformation = { 0 };

        databaseFileName = PhConcatStrings2(config->DestinationDirectory->Buffer, L"\\" EN_DATABASE_NAME);
        PhQueryFullAttributesFileWin32(databaseFileName->Buffer, &oldInformation);

        status = EnCompactDatabase(config, ConsoleMessageHandler);
        RecoverAfterEngineMessages();

        if (NT_SUCCESS(status))
        {
            PhQueryFullAttributesFileWin32(databaseFileName->Buffer, &newInformation);

            wprintf(L"== Successfully compacted database.\n");
            wprintf(L"== Old size: %s bytes\n", oldInformation.EndOfFile.QuadPart != 0 ? PhFormatUInt64(oldInformation.EndOfFile.QuadPart, TRUE)->Buffer : L"???");
            wprintf(L"== New size: %s bytes\n", newInformation.EndOfFile.QuadPart != 0 ? PhFormatUInt64(newInformation.EndOfFile.QuadPart, TRUE)->Buffer : L"???");
        }
        else
        {
            wprintf(L"== Error: 0x%x\n          %s\n", status, PhGetStringOrDefault(GetNtMessage(status), L"-"));
            return 1;
        }
    }
    else
    {
        wprintf(L"Unknown command '%s'\n", Command->Buffer);
    }

    return 0;
}

static BOOLEAN NTAPI CommandLineCallback(
    __in_opt PPH_COMMAND_LINE_OPTION Option,
    __in_opt PPH_STRING Value,
    __in_opt PVOID Context
    )
{
    LONG64 integer;

    if (Option)
    {
        switch (Option->Id)
        {
        case OPTION_CONFIGFILENAME:
            PhSwapReference(&ConfigFileName, Value);
            break;
        case OPTION_HELP:
            ShowHelp = TRUE;
            break;
        case OPTION_REVISIONID:
            {
                PH_STRINGREF firstPart;
                PH_STRINGREF secondPart;

                PhSplitStringRefAtChar(&Value->sr, ':', &firstPart, &secondPart);
                PhStringToInteger64(&firstPart, 10, &integer);
                ParameterRevisionId = integer;

                if (secondPart.Length != 0)
                {
                    PhStringToInteger64(&secondPart, 10, &integer);
                    ParameterRevisionId2 = integer;
                }
            }
            break;
        case OPTION_FORCE:
            ParameterForce = TRUE;
            break;
        case OPTION_TIME:
            {
                PH_STRINGREF timeSr;

                timeSr = Value->sr;

                if (PhEndsWithStringRef2(&timeSr, L"min", TRUE))
                {
                    timeSr.Length -= 3 * sizeof(WCHAR);
                    PhStringToInteger64(&timeSr, 10, &integer);
                    ParameterTime = (ULONG64)integer * PH_TICKS_PER_MIN;
                }
                else if (PhEndsWithStringRef2(&timeSr, L"h", TRUE))
                {
                    timeSr.Length -= sizeof(WCHAR);
                    PhStringToInteger64(&timeSr, 10, &integer);
                    ParameterTime = (ULONG64)integer * PH_TICKS_PER_HOUR;
                }
                else if (PhEndsWithStringRef2(&timeSr, L"d", TRUE))
                {
                    timeSr.Length -= sizeof(WCHAR);
                    PhStringToInteger64(&timeSr, 10, &integer);
                    ParameterTime = (ULONG64)integer * PH_TICKS_PER_DAY;
                }
                else
                {
                    wprintf(L"Invalid time specification '%s'\n", Value->Buffer);
                    return FALSE;
                }
            }
            break;
        }
    }
    else
    {
        if (!Command)
            PhSwapReference(&Command, Value);
        else if (!CommandParameter)
            PhSwapReference(&CommandParameter, Value);
        else if (!CommandParameter2)
            PhSwapReference(&CommandParameter2, Value);
    }

    return TRUE;
}

static PPH_STRING GetNtMessage(
    __in NTSTATUS Status
    )
{
    PPH_STRING string;
    PH_STRINGREF newString;

    string = PhGetNtMessage(Status);

    if (!string)
        return NULL;

    PhTrimToNullTerminatorString(string);
    newString = string->sr;

    while (newString.Length != 0)
    {
        WCHAR c;

        c = newString.Buffer[newString.Length / sizeof(WCHAR) - 1];

        if (c != '\n' && c != '\r' && c != '\t')
            break;

        newString.Length -= sizeof(WCHAR);
    }

    if (newString.Buffer != string->Buffer || newString.Length != string->Length)
    {
        PhSwapReference2(&string, PhCreateStringEx(newString.Buffer, newString.Length));
    }

    return string;
}

static PPH_STRING FormatUtcTime(
    __in PLARGE_INTEGER Time
    )
{
    SYSTEMTIME systemTime;

    PhLargeIntegerToLocalSystemTime(&systemTime, Time);

    return PhFormatDateTime(&systemTime);
}

static VOID RecoverAfterEngineMessages(
    VOID
    )
{
    if (LastLineWasCr)
        putchar('\n');
}

static VOID ConsoleMessageHandler(
    __in ULONG Level,
    __in __assumeRefs(1) PPH_STRING Message
    )
{
    static ULONG ProgressCounter = 0;

    if (PhStartsWithString2(Message, L"Compressing: ", FALSE) ||
        PhStartsWithString2(Message, L"Extracting: ", FALSE))
    {
        // Don't update progress too often.
        if ((ProgressCounter & 0xf) == 0)
        {
            wprintf(L"\r%s", Message->Buffer);
            LastLineWasCr = TRUE;
        }

        ProgressCounter++;
    }
    else
    {
        if (LastLineWasCr)
            putchar('\n');

        if (Level == EN_MESSAGE_WARNING)
            wprintf(L"** Warning ** ");
        if (Level == EN_MESSAGE_ERROR)
            wprintf(L"** ERROR ** ");

        wprintf(L"%s\n", Message->Buffer);
        LastLineWasCr = FALSE;
    }

    PhDereferenceObject(Message);
}

static VOID EnumDb(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in_opt PPH_STRING FileName
    )
{
    PDB_FILE_DIRECTORY_INFORMATION dirInfo;
    ULONG numberOfEntries;
    ULONG i;
    PPH_STRING fileName;

    DbQueryDirectoryFile(Database, File, &dirInfo, &numberOfEntries);

    for (i = 0; i < numberOfEntries; i++)
    {
        PDBF_FILE file;

        if (FileName)
        {
            fileName = PhFormatString(L"%s\\%s", FileName->Buffer, dirInfo[i].FileName->Buffer);
        }
        else
        {
            fileName = dirInfo[i].FileName;
            PhReferenceObject(fileName);
        }

        if (PhIsNullOrEmptyString(CommandParameter) || PhFindStringInStringRef(&fileName->sr, &CommandParameter->sr, TRUE) != -1)
        {
            if (!(dirInfo[i].Attributes & DB_FILE_ATTRIBUTE_DELETE_TAG))
                wprintf(L"%I64u\t%s\n", dirInfo[i].RevisionId, fileName->Buffer);
            else
                wprintf(L"del\t%s\n", fileName->Buffer);
        }

        if (dirInfo[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
        {
            DbCreateFile(Database, &dirInfo[i].FileName->sr, File, 0, 0, 0, NULL, &file);
            EnumDb(Database, file, fileName);
            DbCloseFile(Database, file);
        }

        PhDereferenceObject(fileName);
    }

    DbFreeQueryDirectoryFile(dirInfo, numberOfEntries);
}

static VOID PrintHelp(
    __in_opt PPH_STRING Command
    )
{
    if (Command)
    {
        if (PhEqualString2(Command, L"status", TRUE))
        {
            wprintf(
                L"Usage:\n\tbkc status [-c filename]\n"
                L"\tShows revision information for the database.\n"
                );
            return;
        }
        else if (PhEqualString2(Command, L"diff", TRUE))
        {
            wprintf(
                L"Usage:\n\tbkc diff [-r baserevisionid[:targetrevisionid]] [-c filename]\n"
                L"\tShows differences between the file system and the last backup.\n"
                L"\tAlso shows differences between two revisions.\n"
                L"\n"
                L"Output:\n"
                L"\t+ Z\\example\\doc.txt\tNew file or directory\n"
                L"\t- Z\\example\\file.zip\tDeleted file or directory\n"
                L"\tm Z\\example\\foo.html\tModified file\n"
                L"\ts Z\\example\\.bar\tSwitched (file became directory or vice-versa)\n"
                L"\n"
                L"Examples:\n"
                L"\t* 'bkc diff' will compare the file system with the last backup.\n"
                L"\t* 'bkc diff -r 3:6' will compare revision 6 with revision 3.\n"
                L"\t* 'bkc diff -r 4' will compare the last revision with revision 4.\n"
                );
            return;
        }
        else if (PhEqualString2(Command, L"log", TRUE))
        {
            wprintf(
                L"Usage:\n\tbkc log [targetfilename] [-c filename]\n"
                L"\tShows revisions for a file or directory.\n"
                L"\n"
                L"Output:\n"
                L"\tID\tRevision time\t\tModified time\t\tSize\n"
                L"\t2\t12:30:31 AM 10/9/1899\t9:12:02 PM 9/9/1899\t7,369\n"
                L"\t1\t12:30:41 AM 9/9/1899\t5:12:02 PM 4/9/1895\t5,123\n"
                L"\n"
                L"\tID: The revision ID\n"
                L"\tRevision time: When the file or directory was backed up\n"
                L"\tModified time: When the file was modified (blank for directories)\n"
                L"\tSize: The size of the file (0 for directories)\n"
                );
            return;
        }
        else if (PhEqualString2(Command, L"backup", TRUE))
        {
            wprintf(
                L"Usage:\n\tbkc backup [-c filename]\n"
                L"\tPerforms a backup (creates a new revision).\n"
                L"\n"
                L"Examples:\n"
                L"\t* The database does not exist. Running 'bkc backup' will create it and perform the initial backup.\n"
                L"\t* The database is at revision 12 and there are changes. Running 'bkc backup' will perform a backup and create revision 13.\n"
                L"\t* The database is at revision 12 and there are no changes. Running 'bkc backup' will do nothing.\n"
                L"\n"
                L"Output:\n"
                L"\tUse 'bkc --help diff' to see possible diff lines.\n"
                );
            return;
        }
        else if (PhEqualString2(Command, L"revert", TRUE))
        {
            wprintf(
                L"Usage:\n\tbkc revert -r revisionid [-c filename]\n"
                L"\tReverts to the specified revision.\n"
                L"\n"
                L"Examples:\n"
                L"\t* The database contains revisions 4 .. 9. Running 'bkc revert -r 6' will delete revisions 7 to 9, reverting the database to revision 6.\n"
                L"\t* The database contains only revision 1. Running 'bkc revert -r 1' will do nothing.\n"
                );
            return;
        }
        else if (PhEqualString2(Command, L"trim", TRUE))
        {
            wprintf(
                L"Usage:\n\tbkc trim -r revisionid [-c filename] [-t timespec]\n"
                L"\tDeletes old revisions up to the specified revision.\n"
                L"\tIf a time span is specified using '-t', all revisions older than the specified time span will be deleted.\n"
                L"\n"
                L"Examples:\n"
                L"\t* The database contains revisions 4 .. 9. Running 'bkc trim -r 6' will delete revisions 4 and 5.\n"
                L"\t* The database contains revisions 4 .. 9. Revision 5 was created 40 days ago but revision 6 was created 10 days ago. "
                L"Running 'bkc trim -t 30d' will delete revisions 4 and 5.\n"
                L"\t* The database contains only revision 1. Running 'bkc trim -r 1' will do nothing.\n"
                );
            return;
        }
        else if (PhEqualString2(Command, L"restore", TRUE))
        {
            wprintf(
                L"Usage:\n\tbkc restore <fromfile> [tofileordirectory] [-c filename] [-r revisionid]\n"
                L"\tRestores a file or directory.\n"
                L"\tIf no revision ID is specified, the last (newest) revision is used.\n"
                L"\tNote that <fromfile> must be a database-format file name.\n"
                L"\n"
                L"Examples:\n"
                L"\t* To restore a file that was originally at D:\\foo\\bar.zip, use 'bkc restore D\\foo\\bar.zip'.\n"
                L"\t* To restore G:\\some\\file.txt from revision 3, use 'bkc restore -r 3 restore G\\some\\file.txt'.\n"
                );
            return;
        }
        else if (PhEqualString2(Command, L"list", TRUE))
        {
            wprintf(
                L"Usage:\n\tbkc list [filter] [-c filename]\n"
                L"\tLists the files in the database.\n"
                L"\tThis displays the full directory structure of the database.\n"
                );
            return;
        }
        else if (PhEqualString2(Command, L"compact", TRUE))
        {
            wprintf(
                L"Usage:\n\tbkc compact [-c filename]\n"
                L"\tAttempts to reduce the size of the database.\n"
                );
            return;
        }
        else if (PhEqualString2(Command, L"config", TRUE))
        {
            wprintf(
                L"Before you can use Backup, you must create a configuration file.\n"
                L"Backup automatically uses the name \"config.ini\" unless this is overriden with the '-c' option.\n"
                L"\n"
                L"Example:"
                L"\n"
                L"\t[Source]\n"
                L"\tDirectory=C:\\Users\\username\n"
                L"\tDirectory=D:\\Other Documents\n"
                L"\tFile=E:\\Important File.txt\n"
                L"\tUseShadowCopy=1\n"
                L"\t[SourceFilters]\n"
                L"\tExclude=C:\\Users\\username\\Documents\\uselessfile.htm\n"
                L"\t[Destination]\n"
                L"\tDirectory=K:\\backup\n"
                L"\tUseTransactions=1\n"
                L"\n"
                L"Sections:\n"
                L"\n"
                L"[Map]\n"
                L"\t<dbname> = <filename>\n"
                L"\t\tMaps from a DB prefix to a file system name. For example:\n"
                L"\t\t\tSomeAlias = E:\\Some\\Path\n"
                L"\t\tIn [Source], \"Directory=SomeAlias\\file.txt\" would then specify\n"
                L"\t\t\tE:\\Some\\Path\\file.txt\n"
                L"\t\t<dbname> must not contain any backslashes.\n"
                L"\n"
                L"[Source]\n"
                L"\tDirectory = <directoryname>\n"
                L"\t\tIncludes a directory in the backup.\n"
                L"\tFile = <filename>\n"
                L"\t\tIncludes a file in the backup.\n"
                L"\tUseShadowCopy = 1 or 0\n"
                L"\t\tIf set to 1, backups will be made using shadow copies.\n"
                L"\t\tUse of this feature is recommended as locked files cannot be\n"
                L"\t\tread otherwise.\n"
                L"\n"
                L"[SourceFilters]\n"
                L"\tInclude = <pattern>\n"
                L"\t\tSpecifies an include filter. If include filters exist and a\n"
                L"\t\tfile or directory does not match any include filters, it will\n"
                L"\t\tbe excluded.\n"
                L"\tIncludeSize = <pattern>|<expression>\n"
                L"\t\tSpecifies an include filter based on file size. The filter only\n"
                L"\t\tapplies to files matching <pattern>. <expression> consists of\n"
                L"\t\ta greater than or less than symbol followed by a size.\n"
                L"\t\tFor example:\n"
                L"\t\t\tIncludeSize = G:\\files\\*|>4MB\n"
                L"\t\tapplies to files bigger than 4MB in G:\\files\\.\n"
                L"\tExclude = <pattern>\n"
                L"\tExcludeSize = <pattern>|<expression>\n"
                L"\t\tSimilar to Include and IncludeSize, except that matching files\n"
                L"\t\tare excluded.\n"
                L"\n"
                L"[Destination]\n"
                L"\tDirectory = <directoryname>\n"
                L"\t\tSpecifies the backup destination directory. Make sure that the\n"
                L"\t\tdirectory does not contain any files initially.\n"
                L"\tUseTransactions = 1 or 0\n"
                L"\t\tIf set to 1, transactions will be used for file I/O.\n"
                L"\t\tUse of this feature is recommended if the backup destination is\n"
                L"\t\ton a NTFS file system.\n"
                L"\n"
                L"Notes:\n"
                L"\n"
                L"The database is stored in db.bk in the destination directory. This "
                L"database contains information about all backup revisions. Data for "
                L"each revision is stored in .7z files in the same directory. "
                L"Database file names differ from names in the file system. The colon "
                L"in drive letter specifications is removed, and the <dbname> in any "
                L"mappings (the [Map] section) is used for database file names."
                );
            return;
        }
        else
        {
            wprintf(L"Unknown command '%s'\n", Command->Buffer);
        }
    }

    wprintf(
        L"Usage: bkc command [options]\n"
        L"Commands:\n"
        L"\tstatus\t\tShows information about the database.\n"
        L"\tdiff\t\tCompares a revision with the file system or a revision.\n"
        L"\tlog\t\tShows revisions for a file or directory.\n"
        L"\tbackup\t\tPerforms a backup.\n"
        L"\trevert\t\tReverts to a revision.\n"
        L"\ttrim\t\tDeletes old revisions.\n"
        L"\trestore\t\tRestores a file or directory.\n"
        L"\tlist\t\tLists or searches for files in the database.\n"
        L"\tcompact\t\tAttempts to reduce the size of the database.\n"
        L"\n"
        L"Options:\n"
        L"\t-c filename\tSpecifies the location of the configuration file.\n"
        L"\t-r revisionid\tSpecifies a revision ID to use.\n"
        L"\t-f, --force\tAutomatically overwrites files.\n"
        L"\t-t timespec\tSpecifies a time span (e.g. '10min', '4h', '30d').\n"
        L"\n"
        L"Use 'bkc --help <command>' to display help for a specific command.\n"
        L"Use 'bkc --help config' to display help for the configuration file.\n"
        );
}

static PPH_STRING FixMultipleBackslashes(
    __in PPH_STRINGREF String
    )
{
    PH_STRING_BUILDER sb;
    PWCHAR s;
    SIZE_T count;
    BOOLEAN rejectBackslashes;
    WCHAR c;

    s = String->Buffer;
    count = String->Length / sizeof(WCHAR);
    rejectBackslashes = FALSE;

    if (count == 0)
        return PhReferenceEmptyString();

    PhInitializeStringBuilder(&sb, String->Length);

    do
    {
        c = *s;

        if (c == '\\')
        {
            if (!rejectBackslashes)
            {
                PhAppendCharStringBuilder(&sb, c);
                rejectBackslashes = TRUE;
            }
        }
        else
        {
            PhAppendCharStringBuilder(&sb, c);
            rejectBackslashes = FALSE;
        }

        s++;
    } while (--count != 0);

    return PhFinalStringBuilderString(&sb);
}
