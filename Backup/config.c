/*
 * Backup -
 *   configuration manager
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
#include "config.h"

PPH_STRING BkpCreateStringFromFileBuffer(
    __in PVOID Buffer,
    __in SIZE_T Size
    )
{
    PPH_STRING string;
    ULONG result;
    USHORT byteOrderMark;
    SIZE_T count;
    PWCHAR c;

    result = -1;
    RtlIsTextUnicode(Buffer, (ULONG)Size, &result);

    if (Size >= 2)
        byteOrderMark = *(PUSHORT)Buffer;
    else
        byteOrderMark = 0;

    if ((result & IS_TEXT_UNICODE_ODD_LENGTH) && Size >= 3 && byteOrderMark == 0xbbef && *((PUCHAR)Buffer + 2) == 0xbf)
    {
        // UTF-8 (treat as ANSI)
        string = PhCreateStringFromAnsiEx((PCHAR)Buffer + 3, Size - 3);
    }
    else if (result & (IS_TEXT_UNICODE_UNICODE_MASK | IS_TEXT_UNICODE_REVERSE_MASK))
    {
        if (byteOrderMark == 0xfeff || byteOrderMark == 0xfffe)
        {
            // UTF-16 (treat as UCS-16)

            string = PhCreateStringEx((PWSTR)((PCHAR)Buffer + 2), Size - 2);

            if (byteOrderMark == 0xfffe)
            {
                // Convert from big-endian to little-endian.

                count = string->Length / sizeof(WCHAR);
                c = string->Buffer;

                if (count != 0)
                {
                    do
                    {
                        *c = _byteswap_ushort(*c);
                        c++;
                    } while (--count != 0);
                }
            }
        }
        else
        {
            string = PhCreateStringEx(Buffer, Size);
        }
    }
    else
    {
        string = PhCreateStringFromAnsiEx(Buffer, Size);
    }

    return string;
}

NTSTATUS BkCreateConfigFromFile(
    __in PWSTR FileName,
    __out PBK_CONFIG *Config
    )
{
    NTSTATUS status;
    HANDLE fileHandle;
    LARGE_INTEGER fileSize;
    PVOID viewBase;
    SIZE_T viewSize;
    PPH_STRING string;

    status = PhCreateFileWin32(
        &fileHandle,
        FileName,
        FILE_GENERIC_READ | FILE_GENERIC_EXECUTE,
        0,
        FILE_SHARE_READ,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (!NT_SUCCESS(status))
        return status;

    if (!NT_SUCCESS(status = PhGetFileSize(fileHandle, &fileSize)))
    {
        NtClose(fileHandle);
        return status;
    }

    status = PhMapViewOfEntireFile(
        NULL,
        fileHandle,
        TRUE,
        &viewBase,
        &viewSize
        );

    if (NT_SUCCESS(status))
    {
        string = BkpCreateStringFromFileBuffer(viewBase, (SIZE_T)fileSize.QuadPart);

        if (BkCreateConfigFromString(&string->sr, Config))
            status = STATUS_SUCCESS;
        else
            status = STATUS_UNSUCCESSFUL;

        PhDereferenceObject(string);

        NtUnmapViewOfSection(NtCurrentProcess(), viewBase);
    }

    NtClose(fileHandle);

    return status;
}

BOOLEAN BkpIsWhitespaceChar(
    __in WCHAR Char
    )
{
    if (Char == ' ' || Char == '\t' || Char == '\r' || Char == '\f' || Char == '\v')
        return TRUE;

    return FALSE;
}

VOID BkpTrimString(
    __inout PPH_STRINGREF String
    )
{
    while (String->Length != 0 && BkpIsWhitespaceChar(String->Buffer[0]))
    {
        String->Buffer++;
        String->Length -= sizeof(WCHAR);
    }

    while (String->Length != 0 && BkpIsWhitespaceChar(String->Buffer[String->Length / sizeof(WCHAR) - 1]))
    {
        String->Length -= sizeof(WCHAR);
    }
}

ULONG BkpSectionNameToNumber(
    __in PPH_STRINGREF SectionName
    )
{
    if (PhEqualStringRef2(SectionName, L"Map", TRUE))
        return BK_CONFIG_SECTION_MAP;
    if (PhEqualStringRef2(SectionName, L"Source", TRUE))
        return BK_CONFIG_SECTION_SOURCE;
    if (PhEqualStringRef2(SectionName, L"SourceFilters", TRUE))
        return BK_CONFIG_SECTION_SOURCEFILTERS;
    if (PhEqualStringRef2(SectionName, L"Destination", TRUE))
        return BK_CONFIG_SECTION_DESTINATION;

    return 0;
}

BOOLEAN BkCreateConfigFromString(
    __in PPH_STRINGREF String,
    __out PBK_CONFIG *Config
    )
{
    PBK_CONFIG config;
    PH_STRINGREF currentLine;
    PH_STRINGREF remainingString;
    ULONG currentSection;

    config = PhAllocate(sizeof(BK_CONFIG));
    memset(config, 0, sizeof(BK_CONFIG));
    config->MapFromList = PhCreateList(8);
    config->MapToList = PhCreateList(8);
    config->SourceDirectoryList = PhCreateList(8);
    config->SourceFileList = PhCreateList(8);
    config->IncludeList = PhCreateList(8);
    config->ExcludeList = PhCreateList(8);
    config->IncludeSizeList = PhCreateList(8);
    config->ExcludeSizeList = PhCreateList(8);

    remainingString = *String;
    currentSection = 0;

    while (remainingString.Length != 0)
    {
        PhSplitStringRefAtChar(&remainingString, '\n', &currentLine, &remainingString);
        BkpTrimString(&currentLine);

        if (currentLine.Length != 0 && currentLine.Buffer[0] == ';')
            continue;

        if (currentLine.Length >= 2 * sizeof(WCHAR) &&
            currentLine.Buffer[0] == '[' &&
            currentLine.Buffer[currentLine.Length / sizeof(WCHAR) - 1] == ']')
        {
            // Change section
            currentLine.Buffer++;
            currentLine.Length -= 2 * sizeof(WCHAR);
            currentSection = BkpSectionNameToNumber(&currentLine);
        }
        else if (currentLine.Length != 0)
        {
            PH_STRINGREF lhs;
            PH_STRINGREF rhs;
            LONG64 integer;

            PhSplitStringRefAtChar(&currentLine, '=', &lhs, &rhs);
            BkpTrimString(&lhs);
            BkpTrimString(&rhs);

            switch (currentSection)
            {
            case BK_CONFIG_SECTION_MAP:
                {
                    PhAddItemList(config->MapFromList, PhCreateStringEx(lhs.Buffer, lhs.Length));
                    PhAddItemList(config->MapToList, PhCreateStringEx(rhs.Buffer, rhs.Length));
                }
                break;
            case BK_CONFIG_SECTION_SOURCE:
                {
                    if (PhEqualStringRef2(&lhs, L"Directory", TRUE))
                    {
                        if (rhs.Length != 0)
                            PhAddItemList(config->SourceDirectoryList, PhCreateStringEx(rhs.Buffer, rhs.Length));
                    }
                    else if (PhEqualStringRef2(&lhs, L"File", TRUE))
                    {
                        if (rhs.Length != 0)
                            PhAddItemList(config->SourceFileList, PhCreateStringEx(rhs.Buffer, rhs.Length));
                    }
                    else if (PhEqualStringRef2(&lhs, L"UseShadowCopy", TRUE))
                    {
                        PhStringToInteger64(&rhs, 10, &integer);
                        config->UseShadowCopy = (ULONG)integer;
                    }
                }
                break;
            case BK_CONFIG_SECTION_SOURCEFILTERS:
                {
                    if (PhEqualStringRef2(&lhs, L"Include", TRUE))
                    {
                        if (rhs.Length != 0)
                            PhAddItemList(config->IncludeList, PhCreateStringEx(rhs.Buffer, rhs.Length));
                    }
                    else if (PhEqualStringRef2(&lhs, L"Exclude", TRUE))
                    {
                        if (rhs.Length != 0)
                            PhAddItemList(config->ExcludeList, PhCreateStringEx(rhs.Buffer, rhs.Length));
                    }
                    else if (PhEqualStringRef2(&lhs, L"IncludeSize", TRUE))
                    {
                        if (rhs.Length != 0)
                            PhAddItemList(config->IncludeSizeList, PhCreateStringEx(rhs.Buffer, rhs.Length));
                    }
                    else if (PhEqualStringRef2(&lhs, L"ExcludeSize", TRUE))
                    {
                        if (rhs.Length != 0)
                            PhAddItemList(config->ExcludeSizeList, PhCreateStringEx(rhs.Buffer, rhs.Length));
                    }
                }
                break;
            case BK_CONFIG_SECTION_DESTINATION:
                {
                    if (PhEqualStringRef2(&lhs, L"Directory", TRUE))
                    {
                        PhSwapReference2(&config->DestinationDirectory, PhCreateStringEx(rhs.Buffer, rhs.Length));
                    }
                    else if (PhEqualStringRef2(&lhs, L"CompressionLevel", TRUE))
                    {
                        PhStringToInteger64(&rhs, 10, &integer);
                        config->CompressionLevel = (ULONG)integer;
                    }
                    else if (PhEqualStringRef2(&lhs, L"UseTransactions", TRUE))
                    {
                        PhStringToInteger64(&rhs, 10, &integer);
                        config->UseTransactions = (ULONG)integer;
                    }
                }
                break;
            }
        }
    }

    *Config = config;

    return TRUE;
}

VOID BkDereferenceStringList(
    __in PPH_LIST List
    )
{
    ULONG i;

    for (i = 0; i < List->Count; i++)
    {
        PhDereferenceObject(List->Items[i]);
    }

    PhDereferenceObject(List);
}

VOID BkFreeConfig(
    __in PBK_CONFIG Config
    )
{
    BkDereferenceStringList(Config->MapFromList);
    BkDereferenceStringList(Config->MapToList);
    BkDereferenceStringList(Config->SourceDirectoryList);
    BkDereferenceStringList(Config->SourceFileList);
    BkDereferenceStringList(Config->IncludeList);
    BkDereferenceStringList(Config->ExcludeList);
    BkDereferenceStringList(Config->IncludeSizeList);
    BkDereferenceStringList(Config->ExcludeSizeList);
    PhDereferenceObject(Config->DestinationDirectory);

    PhFree(Config);
}
