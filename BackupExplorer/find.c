/*
 * Backup Explorer -
 *   find dialog
 *
 * Copyright (C) 2013 wj32
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

#include "explorer.h"
#include <windowsx.h>
#include "findp.h"

#define WM_BE_SEARCH_UPDATE (WM_APP + 801)
#define WM_BE_SEARCH_FINISHED (WM_APP + 802)

HWND BeFindDialogHandle;
static PH_LAYOUT_MANAGER LayoutManager;
static HWND BeFindListHandle;
static ULONG BeFindListSortColumn;
static PH_SORT_ORDER BeFindListSortOrder;

static HANDLE SearchThreadHandle = NULL;
static BOOLEAN SearchStop;
static BOOLEAN SearchWordMatch = TRUE;
static PPH_STRING SearchString;
static PPH_LIST SearchResults = NULL;
static ULONG SearchResultsAddIndex;
static ULONG SearchResultsAddThreshold;
static PH_QUEUED_LOCK SearchResultsLock = PH_QUEUED_LOCK_INIT;

static PPH_LIST SearchResultsLocal;

VOID BeShowFindDialog(
    VOID
    )
{
    if (!SearchResultsLocal)
        SearchResultsLocal = PhCreateList(10);

    if (!BeFindDialogHandle)
    {
        BeFindDialogHandle = CreateDialog(
            BeInstanceHandle,
            MAKEINTRESOURCE(IDD_FIND),
            BeWindowHandle,
            BeFindDlgProc
            );
        BeRegisterDialog(BeFindDialogHandle);
    }

    ShowWindow(BeFindDialogHandle, SW_SHOW);
    SetForegroundWindow(BeFindDialogHandle);
}

INT_PTR CALLBACK BeFindDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            PhInitializeLayoutManager(&LayoutManager, hwndDlg);
            PhAddLayoutItem(&LayoutManager, GetDlgItem(hwndDlg, IDC_FILTER), NULL, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
            PhAddLayoutItem(&LayoutManager, GetDlgItem(hwndDlg, IDOK), NULL, PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
            PhAddLayoutItem(&LayoutManager, GetDlgItem(hwndDlg, IDC_RESULTS), NULL, PH_ANCHOR_ALL);

            BeFindListHandle = GetDlgItem(hwndDlg, IDC_RESULTS);
            PhSetControlTheme(BeFindListHandle, L"explorer");
            TreeNew_SetCallback(BeFindListHandle, BeFindListTreeNewCallback, NULL);
            PhAddTreeNewColumn(BeFindListHandle, BEFTNC_FILE, TRUE, L"File", 350, PH_ALIGN_LEFT, -2, 0);
            PhAddTreeNewColumnEx(BeFindListHandle, BEFTNC_SIZE, TRUE, L"Size", 80, PH_ALIGN_RIGHT, 0, DT_RIGHT, TRUE);
            PhAddTreeNewColumnEx(BeFindListHandle, BEFTNC_BACKUPTIME, TRUE, L"Time Modified", 140, PH_ALIGN_LEFT, 1, 0, TRUE);
            PhAddTreeNewColumnEx(BeFindListHandle, BEFTNC_LASTREVISION, TRUE, L"Last Revision", 55, PH_ALIGN_RIGHT, 2, DT_RIGHT, TRUE);
            TreeNew_SetSort(BeFindListHandle, BEFTNC_FILE, AscendingSortOrder);
        }
        break;
    case WM_SHOWWINDOW:
        {
            SetFocus(GetDlgItem(hwndDlg, IDC_FILTER));
            Edit_SetSel(GetDlgItem(hwndDlg, IDC_FILTER), 0, -1);
        }
        break;
    case WM_CLOSE:
        {
            // Hide, don't close.
            ShowWindow(hwndDlg, SW_HIDE);
            SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, 0);
        }
        return TRUE;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&LayoutManager);
        }
        break;
    case WM_SETCURSOR:
        {
            if (SearchThreadHandle)
            {
                SetCursor(LoadCursor(NULL, IDC_WAIT));
                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDOK:
                {
                    // Don't continue if the user requested cancellation.
                    if (SearchStop)
                        break;

                    if (!SearchThreadHandle)
                    {
                        ULONG i;

                        // Cleanup previous results.

                        PhClearList(SearchResultsLocal);
                        TreeNew_NodesStructured(BeFindListHandle); // IMPORTANT - let treenew know that the items don't exist before freeing below!

                        if (SearchResults)
                        {
                            for (i = 0; i < SearchResults->Count; i++)
                                BeDestroyResultNode(SearchResults->Items[i]);

                            PhDereferenceObject(SearchResults);
                        }

                        // Start the search.

                        SearchString = PhGetWindowText(GetDlgItem(hwndDlg, IDC_FILTER));
                        PhUpperString(SearchString);
                        SearchResults = PhCreateList(128);
                        SearchResultsAddIndex = 0;
                        SearchResultsAddThreshold = 1;

                        SearchThreadHandle = PhCreateThread(0, BeFindFilesThreadStart, NULL);

                        if (!SearchThreadHandle)
                        {
                            PhDereferenceObject(SearchString);
                            PhDereferenceObject(SearchResults);
                            SearchString = NULL;
                            SearchResults = NULL;
                            break;
                        }

                        SetDlgItemText(hwndDlg, IDOK, L"Cancel");

                        SetCursor(LoadCursor(NULL, IDC_WAIT));
                    }
                    else
                    {
                        SearchStop = TRUE;
                        EnableWindow(GetDlgItem(hwndDlg, IDOK), FALSE);
                    }
                }
                break;
            case IDCANCEL:
                SendMessage(hwndDlg, WM_CLOSE, 0, 0);
                break;
            }
        }
        break;
    case WM_BE_SEARCH_UPDATE:
        {
            ULONG newCount;

            PhAcquireQueuedLockExclusive(&SearchResultsLock);
            newCount = SearchResults->Count - SearchResultsAddIndex;
            PhAddItemsList(SearchResultsLocal, SearchResults->Items + SearchResultsAddIndex, newCount);
            SearchResultsAddIndex = SearchResults->Count;
            PhReleaseQueuedLockExclusive(&SearchResultsLock);

            if (newCount != 0)
                TreeNew_NodesStructured(BeFindListHandle);
        }
        break;
    case WM_BE_SEARCH_FINISHED:
        {
            // Add any un-added items.
            SendMessage(hwndDlg, WM_BE_SEARCH_UPDATE, 0, 0);

            PhDereferenceObject(SearchString);

            NtWaitForSingleObject(SearchThreadHandle, FALSE, NULL);
            NtClose(SearchThreadHandle);
            SearchThreadHandle = NULL;
            SearchStop = FALSE;

            SetDlgItemText(hwndDlg, IDOK, L"Find");
            EnableWindow(GetDlgItem(hwndDlg, IDOK), TRUE);

            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
        break;
    }

    return FALSE;
}

VOID BeDestroyResultNode(
    _In_ PBE_RESULT_NODE Node
    )
{
    PhSwapReference(&Node->FileName, NULL);
    PhSwapReference(&Node->EndOfFileString, NULL);
    PhSwapReference(&Node->LastBackupTimeString, NULL);
    PhSwapReference(&Node->LastRevisionIdString, NULL);
    PhFree(Node);
}

PBE_RESULT_NODE BeGetSelectedResultNode(
    VOID
    )
{
    ULONG count;
    ULONG i;

    count = TreeNew_GetFlatNodeCount(BeFindListHandle);

    for (i = 0; i < count; i++)
    {
        PBE_RESULT_NODE node = (PBE_RESULT_NODE)TreeNew_GetFlatNode(BeFindListHandle, i);

        if (node->Node.Selected)
            return node;
    }

    return NULL;
}

#define SORT_FUNCTION(Column) BeFindListTreeNewCompare##Column

#define BEGIN_SORT_FUNCTION(Column) static int __cdecl BeFindListTreeNewCompare##Column( \
    _In_ void *_context, \
    _In_ const void *_elem1, \
    _In_ const void *_elem2 \
    ) \
{ \
    PBE_RESULT_NODE node1 = *(PBE_RESULT_NODE *)_elem1; \
    PBE_RESULT_NODE node2 = *(PBE_RESULT_NODE *)_elem2; \
    int sortResult = 0;

#define END_SORT_FUNCTION \
    if (sortResult == 0) \
        sortResult = PhCompareString(node1->FileName, node2->FileName, TRUE); \
    \
    return PhModifySort(sortResult, BeFindListSortOrder); \
}

BEGIN_SORT_FUNCTION(File)
{
    sortResult = PhCompareString(node1->FileName, node2->FileName, TRUE);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Size)
{
    sortResult = uint64cmp(node1->EndOfFile.QuadPart, node2->EndOfFile.QuadPart);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(BackupTime)
{
    sortResult = uint64cmp(node1->LastBackupTime.QuadPart, node2->LastBackupTime.QuadPart);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(LastRevision)
{
    sortResult = uint64cmp(node1->LastRevisionId, node2->LastRevisionId);
}
END_SORT_FUNCTION

BOOLEAN BeFindListTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    )
{
    PBE_RESULT_NODE node;

    switch (Message)
    {
    case TreeNewGetChildren:
        {
            static PVOID sortFunctions[] =
            {
                SORT_FUNCTION(File),
                SORT_FUNCTION(Size),
                SORT_FUNCTION(BackupTime),
                SORT_FUNCTION(LastRevision)
            };
            PPH_TREENEW_GET_CHILDREN getChildren = Parameter1;
            int (__cdecl *sortFunction)(void *, const void *, const void *);

            if (getChildren->Node)
                return FALSE;

            if (BeFindListSortColumn < BEFTNC_MAXIMUM)
                sortFunction = sortFunctions[BeFindListSortColumn];
            else
                sortFunction = NULL;

            if (sortFunction)
            {
                qsort_s(SearchResultsLocal->Items, SearchResultsLocal->Count, sizeof(PVOID), sortFunction, NULL);
            }

            getChildren->Children = (PPH_TREENEW_NODE *)SearchResultsLocal->Items;
            getChildren->NumberOfChildren = SearchResultsLocal->Count;
        }
        return TRUE;
    case TreeNewIsLeaf:
        {
            PPH_TREENEW_IS_LEAF isLeaf = Parameter1;

            isLeaf->IsLeaf = TRUE;
        }
        return TRUE;
    case TreeNewGetCellText:
        {
            PPH_TREENEW_GET_CELL_TEXT getCellText = Parameter1;

            node = (PBE_RESULT_NODE)getCellText->Node;

            switch (getCellText->Id)
            {
            case BEFTNC_FILE:
                getCellText->Text = node->FileName->sr;
                break;
            case BEFTNC_SIZE:
                getCellText->Text = PhGetStringRef(node->EndOfFileString);
                break;
            case BEFTNC_BACKUPTIME:
                getCellText->Text = PhGetStringRef(node->LastBackupTimeString);
                break;
            case BEFTNC_LASTREVISION:
                getCellText->Text = PhGetStringRef(node->LastRevisionIdString);
                break;
            default:
                return FALSE;
            }
        }
        return TRUE;
    case TreeNewSortChanged:
        {
            TreeNew_GetSort(hwnd, &BeFindListSortColumn, &BeFindListSortOrder);
            // Force a rebuild to sort the items.
            TreeNew_NodesStructured(hwnd);
        }
        return TRUE;
    case TreeNewSelectionChanged:
        {
            PBE_RESULT_NODE node;

            node = BeGetSelectedResultNode();

            if (node)
            {
                BeSetCurrentRevision(node->LastRevisionId);
                BeSelectFullPath(&node->FileName->sr);
            }
        }
        break;
    }

    return FALSE;
}

static BOOLEAN WordMatch(
    _In_ PPH_STRINGREF Text,
    _In_ PPH_STRINGREF Search,
    _In_ BOOLEAN IgnoreCase
    )
{
    PH_STRINGREF part;
    PH_STRINGREF remainingPart;

    remainingPart = *Search;

    while (remainingPart.Length != 0)
    {
        PhSplitStringRefAtChar(&remainingPart, ' ', &part, &remainingPart);

        if (part.Length != 0)
        {
            if (PhFindStringInStringRef(Text, &part, IgnoreCase) == -1)
                return FALSE;
        }
    }

    return TRUE;
}

BOOLEAN BeStringCompareFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PPH_STRING entry1 = *(PPH_STRING *)Entry1;
    PPH_STRING entry2 = *(PPH_STRING *)Entry2;

    return PhEqualString(entry1, entry2, FALSE);
}

ULONG BeStringHashFunction(
    _In_ PVOID Entry
    )
{
    PPH_STRING entry = *(PPH_STRING *)Entry;

    return PhHashBytes((PUCHAR)entry->Buffer, entry->Length);
}

static VOID EnumDb(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_opt_ PPH_STRING FileName,
    _In_ ULONGLONG RevisionId,
    _In_ PPH_HASHTABLE NamesSeen
    )
{
    PDB_FILE_DIRECTORY_INFORMATION dirInfo;
    ULONG numberOfEntries;
    ULONG i;
    PPH_STRING fileName;

    if (SearchStop)
        return;

    if (!NT_SUCCESS(DbQueryDirectoryFile(Database, File, &dirInfo, &numberOfEntries)))
        return;

    for (i = 0; i < numberOfEntries; i++)
    {
        PDBF_FILE file;
        PPH_STRING upperFileName;

        if (SearchStop)
            break;

        if (FileName)
        {
            fileName = PhFormatString(L"%s\\%s", FileName->Buffer, dirInfo[i].FileName->Buffer);
        }
        else
        {
            fileName = dirInfo[i].FileName;
            PhReferenceObject(fileName);
        }

        upperFileName = PhDuplicateString(fileName);
        PhUpperString(upperFileName);

        if (!(dirInfo[i].Attributes & DB_FILE_ATTRIBUTE_DELETE_TAG))
        {
            if (!PhFindEntryHashtable(NamesSeen, &upperFileName) && (
                (!SearchWordMatch && PhFindStringInStringRef(&upperFileName->sr, &SearchString->sr, FALSE) != -1) ||
                (SearchWordMatch && WordMatch(&upperFileName->sr, &SearchString->sr, FALSE))
                ))
            {
                PBE_RESULT_NODE result;
                SYSTEMTIME systemTime;

                PhAddEntryHashtable(NamesSeen, &upperFileName);
                PhReferenceObject(upperFileName);

                result = PhAllocate(sizeof(BE_RESULT_NODE));
                memset(result, 0, sizeof(BE_RESULT_NODE));

                PhInitializeTreeNewNode(&result->Node);
                result->FileName = fileName;
                PhReferenceObject(fileName);
                result->IsDirectory = !!(dirInfo[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY);
                result->EndOfFile = dirInfo[i].EndOfFile;
                result->LastBackupTime = dirInfo[i].LastBackupTime;
                result->LastRevisionId = RevisionId;

                //if (!result->IsDirectory)
                //    result->LastRevisionId = dirInfo[i].RevisionId;

                if (!result->IsDirectory)
                    result->EndOfFileString = PhFormatSize(result->EndOfFile.QuadPart, -1);

                result->LastRevisionIdString = PhFormatUInt64(result->LastRevisionId, TRUE);

                if (result->LastBackupTime.QuadPart != 0)
                {
                    PhLargeIntegerToLocalSystemTime(&systemTime, &result->LastBackupTime);
                    result->LastBackupTimeString = PhFormatDateTime(&systemTime);
                }

                PhAcquireQueuedLockExclusive(&SearchResultsLock);

                PhAddItemList(SearchResults, result);

                // Update the search results in batches.
                if (SearchResults->Count % SearchResultsAddThreshold == 0)
                {
                    PostMessage(BeFindDialogHandle, WM_BE_SEARCH_UPDATE, 0, 0);

                    if (SearchResultsAddThreshold > 10000)
                        SearchResultsAddThreshold += 10000;
                    else if (SearchResultsAddThreshold > 1000)
                        SearchResultsAddThreshold += 1000;
                    else
                        SearchResultsAddThreshold += 10;
                }

                PhReleaseQueuedLockExclusive(&SearchResultsLock);
            }
        }

        if (dirInfo[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
        {
            if (NT_SUCCESS(DbCreateFile(Database, &dirInfo[i].FileName->sr, File, 0, 0, 0, NULL, &file)))
            {
                EnumDb(Database, file, fileName, RevisionId, NamesSeen);
                DbCloseFile(Database, file);
            }
        }

        PhDereferenceObject(upperFileName);
        PhDereferenceObject(fileName);
    }

    DbFreeQueryDirectoryFile(dirInfo, numberOfEntries);
}

NTSTATUS BeFindFilesThreadStart(
    _In_ PVOID Parameter
    )
{
    NTSTATUS status;
    PDB_DATABASE database;
    PPH_HASHTABLE namesSeen;
    ULONGLONG lastRevisionId;
    ULONGLONG firstRevisionId;
    ULONGLONG revisionId;
    PDBF_FILE directory;
    PH_STRINGREF directoryName;
    WCHAR directoryNameBuffer[17];
    PH_HASHTABLE_ENUM_CONTEXT enumContext;
    PPH_STRING *entry;

    // Refuse to search with no filter.
    if (SearchString->Length == 0)
        goto Exit;

    status = EnpOpenDatabase(BeConfig, TRUE, &database);

    if (!NT_SUCCESS(status))
        goto Exit;

    namesSeen = PhCreateHashtable(sizeof(PPH_STRING), BeStringCompareFunction, BeStringHashFunction, 100);

    DbQueryRevisionIdsDatabase(database, &lastRevisionId, &firstRevisionId);

    for (revisionId = lastRevisionId; revisionId >= firstRevisionId; revisionId--)
    {
        if (SearchStop)
            break;

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

        status = DbCreateFile(database, &directoryName, NULL, 0, DB_FILE_OPEN, DB_FILE_ATTRIBUTE_DIRECTORY, NULL, &directory);

        if (!NT_SUCCESS(status))
            continue;

        EnumDb(database, directory, NULL, revisionId, namesSeen);
        DbCloseFile(database, directory);
    }

    PhBeginEnumHashtable(namesSeen, &enumContext);

    while (entry = PhNextEnumHashtable(&enumContext))
        PhDereferenceObject(*entry);

    PhDereferenceObject(namesSeen);
    DbCloseDatabase(database);

Exit:
    PostMessage(BeFindDialogHandle, WM_BE_SEARCH_FINISHED, 0, 0);

    return STATUS_SUCCESS;
}
