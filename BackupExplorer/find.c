#include "explorer.h"
#include <windowsx.h>
#include "findp.h"

#define WM_BE_SEARCH_UPDATE (WM_APP + 801)
#define WM_BE_SEARCH_FINISHED (WM_APP + 802)

HWND BeFindDialogHandle;
static PH_LAYOUT_MANAGER LayoutManager;
static HWND BeFindListHandle;

static HANDLE SearchThreadHandle = NULL;
static BOOLEAN SearchStop;
static PPH_STRING SearchString;
static PPH_LIST SearchResults = NULL;
static ULONG SearchResultsAddIndex;
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
    }

    ShowWindow(BeFindDialogHandle, SW_SHOW);
    SetForegroundWindow(BeFindDialogHandle);
}

INT_PTR CALLBACK BeFindDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
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
            PhAddTreeNewColumn(BeFindListHandle, BEFTNC_SIZE, TRUE, L"Size", 80, PH_ALIGN_RIGHT, 0, DT_RIGHT);
            PhAddTreeNewColumn(BeFindListHandle, BEFTNC_LASTREVISION, TRUE, L"Last Present In", 55, PH_ALIGN_RIGHT, 1, DT_RIGHT);
            PhAddTreeNewColumn(BeFindListHandle, BEFTNC_LASTTIMESTAMP, TRUE, L"Last Time Stamp", 140, PH_ALIGN_LEFT, 2, 0);
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
            PhAcquireQueuedLockExclusive(&SearchResultsLock);
            PhAddItemsList(SearchResultsLocal, SearchResults->Items + SearchResultsAddIndex, SearchResults->Count - SearchResultsAddIndex);
            SearchResultsAddIndex = SearchResults->Count;
            PhReleaseQueuedLockExclusive(&SearchResultsLock);

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
    __in PBE_RESULT_NODE Node
    )
{
    PhSwapReference(&Node->FileName, NULL);
    PhSwapReference(&Node->EndOfFileString, NULL);
    PhSwapReference(&Node->LastRevisionIdString, NULL);
    PhSwapReference(&Node->LastTimeStampString, NULL);
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

BOOLEAN BeFindListTreeNewCallback(
    __in HWND hwnd,
    __in PH_TREENEW_MESSAGE Message,
    __in_opt PVOID Parameter1,
    __in_opt PVOID Parameter2,
    __in_opt PVOID Context
    )
{
    PBE_RESULT_NODE node;

    switch (Message)
    {
    case TreeNewGetChildren:
        {
            PPH_TREENEW_GET_CHILDREN getChildren = Parameter1;

            if (getChildren->Node)
                return FALSE;

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
            case BEFTNC_LASTREVISION:
                getCellText->Text = PhGetStringRef(node->LastRevisionIdString);
                break;
            case BEFTNC_LASTTIMESTAMP:
                getCellText->Text = PhGetStringRef(node->LastTimeStampString);
                break;
            default:
                return FALSE;
            }
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

BOOLEAN BeStringCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    )
{
    PPH_STRING entry1 = *(PPH_STRING *)Entry1;
    PPH_STRING entry2 = *(PPH_STRING *)Entry2;

    return PhEqualString(entry1, entry2, FALSE);
}

ULONG BeStringHashFunction(
    __in PVOID Entry
    )
{
    PPH_STRING entry = *(PPH_STRING *)Entry;

    return PhHashBytes((PUCHAR)entry->Buffer, entry->Length);
}

static VOID EnumDb(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in_opt PPH_STRING FileName,
    __in ULONGLONG RevisionId,
    __in PPH_HASHTABLE NamesSeen
    )
{
    PDB_FILE_DIRECTORY_INFORMATION dirInfo;
    ULONG numberOfEntries;
    ULONG i;
    PPH_STRING fileName;

    if (!NT_SUCCESS(DbQueryDirectoryFile(Database, File, &dirInfo, &numberOfEntries)))
        return;

    for (i = 0; i < numberOfEntries; i++)
    {
        PDBF_FILE file;
        PPH_STRING upperFileName;

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
            if (!PhFindEntryHashtable(NamesSeen, &upperFileName) &&
                PhFindStringInStringRef(&upperFileName->sr, &SearchString->sr, FALSE) != -1)
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
                result->LastRevisionId = RevisionId;
                result->LastTimeStamp = dirInfo[i].TimeStamp;

                //if (!result->IsDirectory)
                //    result->LastRevisionId = dirInfo[i].RevisionId;

                if (!result->IsDirectory)
                    result->EndOfFileString = PhFormatSize(result->EndOfFile.QuadPart, -1);

                result->LastRevisionIdString = PhFormatUInt64(result->LastRevisionId, TRUE);

                PhLargeIntegerToLocalSystemTime(&systemTime, &result->LastTimeStamp);
                result->LastTimeStampString = PhFormatDateTime(&systemTime);

                PhAcquireQueuedLockExclusive(&SearchResultsLock);

                PhAddItemList(SearchResults, result);

                // Update the search results in batches of 40.
                if (SearchResults->Count % 40 == 0)
                    PostMessage(BeFindDialogHandle, WM_BE_SEARCH_UPDATE, 0, 0);

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
    __in PVOID Parameter
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
