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
            TreeNew_SetCallback(BeFindListHandle, BeFindListTreeNewCallback, NULL);
            PhAddTreeNewColumn(BeFindListHandle, BEFTNC_FILE, TRUE, L"File", 350, PH_ALIGN_LEFT, -2, 0);
            PhAddTreeNewColumn(BeFindListHandle, BEFTNC_SIZE, TRUE, L"Size", 80, PH_ALIGN_RIGHT, 0, DT_RIGHT);
            PhAddTreeNewColumn(BeFindListHandle, BEFTNC_REVISIONS, TRUE, L"Revisions", 55, PH_ALIGN_RIGHT, 1, DT_RIGHT);
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
    PhSwapReference(&Node->RevisionsString, NULL);
    PhSwapReference(&Node->EndOfFileString, NULL);
    PhSwapReference(&Node->LastTimeStampString, NULL);
    PhFree(Node);
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
            case BEFTNC_REVISIONS:
                getCellText->Text = PhGetStringRef(node->RevisionsString);
                break;
            case BEFTNC_LASTTIMESTAMP:
                getCellText->Text = PhGetStringRef(node->LastTimeStampString);
                break;
            default:
                return FALSE;
            }
        }
        return TRUE;
    }

    return FALSE;
}

NTSTATUS BeFindFilesThreadStart(
    __in PVOID Parameter
    )
{
    NTSTATUS status;
    PDB_DATABASE database;

    // Refuse to search with no filter.
    if (SearchString->Length == 0)
        goto Exit;

    status = EnpOpenDatabase(BeConfig, TRUE, &database);

    if (!NT_SUCCESS(status))
        goto Exit;



    DbCloseDatabase(database);

Exit:
    PostMessage(BeFindDialogHandle, WM_BE_SEARCH_FINISHED, 0, 0);

    return STATUS_SUCCESS;
}

                    //PhAcquireQueuedLockExclusive(&SearchResultsLock);

                    //PhAddItemList(SearchResults, searchResult);

                    //// Update the search results in batches of 40.
                    //if (SearchResults->Count % 40 == 0)
                    //    PostMessage(BeFindDialogHandle, WM_BE_SEARCH_UPDATE, 0, 0);

                    //PhReleaseQueuedLockExclusive(&SearchResultsLock);