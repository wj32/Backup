/*
 * Backup Explorer -
 *   main window
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
#include <emenu.h>
#include <cpysave.h>
#include <shellapi.h>
#include "explorerp.h"

HWND BeWindowHandle;
HWND BeRevisionListHandle;
PH_LAYOUT_MANAGER BeLayoutManager;
HWND BeProgressWindowHandle;

PEN_FILE_REVISION_INFORMATION BeRevisionInformation;
PPH_STRING BeDatabaseFileName;
ULONGLONG BeCurrentRevision;
PPH_STRING BeTempDatabaseFileName;
PDB_DATABASE BeTempDatabase;
PDBF_FILE BeTempDatabaseHead;

HWND BeFileListHandle;
ULONG BeFileListSortColumn;
PH_SORT_ORDER BeFileListSortOrder;
PBE_FILE_NODE BeRootNode;
PPH_STRING BeSelectedFullPath;

HWND BeLogHandle;
PPH_STRING BeConfigFileName;
PBK_CONFIG BeConfig;

PPH_HASHTABLE BeFileIconHashtable;
PH_STRINGREF BeDirectoryIconKey = PH_STRINGREF_INIT(L"\\");

static INT NTAPI BeRevisionRevisionCompareFunction(
    _In_ PVOID Item1,
    _In_ PVOID Item2,
    _In_opt_ PVOID Context
    )
{
    PEN_FILE_REVISION_INFORMATION item1 = Item1;
    PEN_FILE_REVISION_INFORMATION item2 = Item2;

    return uint64cmp(item1->RevisionId, item2->RevisionId);
}

static INT NTAPI BeRevisionTimeCompareFunction(
    _In_ PVOID Item1,
    _In_ PVOID Item2,
    _In_opt_ PVOID Context
    )
{
    PEN_FILE_REVISION_INFORMATION item1 = Item1;
    PEN_FILE_REVISION_INFORMATION item2 = Item2;

    return uint64cmp(item1->TimeStamp.QuadPart, item2->TimeStamp.QuadPart);
}

INT_PTR CALLBACK BeExplorerDlgProc(
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
            NTSTATUS status;
            PH_STRINGREF databaseName;

            BeWindowHandle = hwndDlg;
            BeRevisionListHandle = GetDlgItem(hwndDlg, IDC_REVISIONS);
            BeFileListHandle = GetDlgItem(hwndDlg, IDC_FILES);
            BeLogHandle = GetDlgItem(hwndDlg, IDC_LOG);

            PhInitializeLayoutManager(&BeLayoutManager, hwndDlg);
            PhAddLayoutItem(&BeLayoutManager, BeRevisionListHandle, NULL, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_BOTTOM);
            PhAddLayoutItem(&BeLayoutManager, GetDlgItem(hwndDlg, IDC_FIND), NULL, PH_ANCHOR_LEFT | PH_ANCHOR_BOTTOM);
            PhAddLayoutItem(&BeLayoutManager, GetDlgItem(hwndDlg, IDC_CLEARLOG), NULL, PH_ANCHOR_LEFT | PH_ANCHOR_BOTTOM);
            PhAddLayoutItem(&BeLayoutManager, GetDlgItem(hwndDlg, IDC_FILES), NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&BeLayoutManager, GetDlgItem(hwndDlg, IDC_LOG), NULL, PH_ANCHOR_LEFT | PH_ANCHOR_BOTTOM | PH_ANCHOR_RIGHT);

            PhCenterWindow(BeWindowHandle, NULL);

            // Revision list

            PhSetListViewStyle(BeRevisionListHandle, FALSE, TRUE);
            PhSetControlTheme(BeRevisionListHandle, L"explorer");
            PhSetExtendedListView(BeRevisionListHandle);
            PhAddListViewColumn(BeRevisionListHandle, 0, 0, 0, LVCFMT_RIGHT, 55, L"Revision");
            PhAddListViewColumn(BeRevisionListHandle, 1, 1, 1, LVCFMT_LEFT, 140, L"Time");

            ExtendedListView_SetSortFast(BeRevisionListHandle, TRUE);
            ExtendedListView_SetCompareFunction(BeRevisionListHandle, 0, BeRevisionRevisionCompareFunction);
            ExtendedListView_SetCompareFunction(BeRevisionListHandle, 1, BeRevisionTimeCompareFunction);
            ExtendedListView_AddFallbackColumn(BeRevisionListHandle, 0);
            ExtendedListView_SetSort(BeRevisionListHandle, 0, DescendingSortOrder);

            // File list

            PhSetControlTheme(BeFileListHandle, L"explorer");
            PhAddTreeNewColumn(BeFileListHandle, BETNC_FILE, TRUE, L"File", 200, PH_ALIGN_LEFT, -2, 0);
            PhAddTreeNewColumnEx(BeFileListHandle, BETNC_SIZE, TRUE, L"Size", 80, PH_ALIGN_RIGHT, 0, DT_RIGHT, TRUE);
            PhAddTreeNewColumnEx(BeFileListHandle, BETNC_BACKUPTIME, TRUE, L"Time Modified", 140, PH_ALIGN_LEFT, 1, 0, TRUE);
            PhAddTreeNewColumnEx(BeFileListHandle, BETNC_REVISION, TRUE, L"Revision", 55, PH_ALIGN_RIGHT, 2, DT_RIGHT, TRUE);
            PhAddTreeNewColumnEx(BeFileListHandle, BETNC_TIMESTAMP, TRUE, L"Revision Time", 140, PH_ALIGN_LEFT, 3, 0, TRUE);
            TreeNew_SetSort(BeFileListHandle, BETNC_FILE, AscendingSortOrder);
            TreeNew_SetCallback(BeFileListHandle, BeFileListTreeNewCallback, NULL);
            TreeNew_SetExtendedFlags(BeFileListHandle, TN_FLAG_ITEM_DRAG_SELECT, TN_FLAG_ITEM_DRAG_SELECT);

            BeRootNode = BeCreateFileNode(NULL, NULL);
            BeRootNode->Node.Expanded = TRUE;
            BeRootNode->IsRoot = TRUE;
            BeRootNode->IsDirectory = TRUE;
            BeRootNode->HasChildren = TRUE;
            BeRootNode->Name = PhCreateString(L"\\");

            // File icons

            BeFileIconHashtable = PhCreateHashtable(sizeof(BE_FILE_ICON_ENTRY), BeFileIconEntryCompareFunction, BeFileIconEntryHashFunction, 4);

            // Open the configuration file.

            if (BeFileName)
                BeConfigFileName = BeFileName;
            else
                BeConfigFileName = BePromptForConfigFileName();

            if (!BeConfigFileName)
            {
                DestroyWindow(hwndDlg);
                break;
            }

            if (!NT_SUCCESS(status = BkCreateConfigFromFile(BeConfigFileName->Buffer, &BeConfig)) || !BeConfig->DestinationDirectory)
            {
                if (BeConfig && !BeConfig->DestinationDirectory)
                    status = STATUS_INVALID_PARAMETER;

                PhShowStatus(hwndDlg, L"Unable to read configuration file", status, 0);
                DestroyWindow(hwndDlg);
                break;
            }

            PhInitializeStringRef(&databaseName, L"\\" EN_DATABASE_NAME);
            BeDatabaseFileName = PhConcatStringRef2(&BeConfig->DestinationDirectory->sr, &databaseName);

            // Get the list of revisions.

            BeLoadRevisionList();
        }
        break;
    case WM_DESTROY:
        {
            BeSetCurrentRevision(0);
            PostQuitMessage(0);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&BeLayoutManager);
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDCANCEL:
                DestroyWindow(hwndDlg);
                break;
            case IDC_FIND:
                BeShowFindDialog();
                break;
            case IDC_CLEARLOG:
                ListBox_ResetContent(BeLogHandle);
                break;
            case ID_FILE_PREVIEW:
                {
                    PBE_FILE_NODE selectedNode = BeGetSelectedFileNode();

                    if (selectedNode)
                    {
                        if (!selectedNode->IsDirectory)
                        {
                            PPH_STRING fullPath;
                            
                            fullPath = BeComputeFullPath(selectedNode);
                            BePreviewSingleFileWithProgress(BeWindowHandle, BeCurrentRevision, &fullPath->sr);
                            PhDereferenceObject(fullPath);
                        }
                    }
                }
                break;
            case ID_FILE_RESTORE:
                {
                    PBE_FILE_NODE selectedNode = BeGetSelectedFileNode();

                    if (selectedNode)
                    {
                        PPH_STRING fullPath;

                        fullPath = BeComputeFullPath(selectedNode);
                        BeRestoreFileOrDirectoryWithProgress(BeWindowHandle, BeCurrentRevision, &fullPath->sr, selectedNode->IsDirectory);
                        PhDereferenceObject(fullPath);
                    }
                }
                break;
            case ID_FILE_REVISIONS:
                {
                    PBE_FILE_NODE selectedNode = BeGetSelectedFileNode();

                    if (selectedNode)
                    {
                        PPH_STRING fullPath;

                        fullPath = BeComputeFullPath(selectedNode);
                        BeShowRevisionsDialog(hwndDlg, &fullPath->sr);
                        PhDereferenceObject(fullPath);
                    }
                }
                break;
            case ID_FILE_COPY:
                {
                    PPH_STRING text;

                    text = PhGetTreeNewText(BeFileListHandle, 0);
                    PhSetClipboardString(BeFileListHandle, &text->sr);
                    PhDereferenceObject(text);
                }
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case LVN_ITEMCHANGED:
                {
                    if (header->hwndFrom == BeRevisionListHandle)
                    {
                        //LPNMITEMACTIVATE itemActivate = (LPNMITEMACTIVATE)header;
                        PEN_FILE_REVISION_INFORMATION revisionInfo = NULL;

                        if (ListView_GetSelectedCount(BeRevisionListHandle) == 1)
                            revisionInfo = PhGetSelectedListViewItemParam(BeRevisionListHandle);

                        if (revisionInfo)
                            BeSetCurrentRevision(revisionInfo->RevisionId);
                    }
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

BOOLEAN BeLoadRevisionList(
    VOID
    )
{
    NTSTATUS status;
    PH_STRINGREF emptySr;
    ULONGLONG lastRevisionId;
    PEN_FILE_REVISION_INFORMATION entries;
    ULONG numberOfEntries;
    ULONG i;

    PhInitializeEmptyStringRef(&emptySr);

    status = EnQueryRevision(BeConfig, BeMessageHandler, &lastRevisionId, NULL, NULL, NULL);

    if (NT_SUCCESS(status))
        status = EnQueryFileRevisions(BeConfig, &emptySr, BeMessageHandler, &entries, &numberOfEntries);

    if (!NT_SUCCESS(status))
    {
        PhShowStatus(BeWindowHandle, L"Unable to read list of revisions", status, 0);
        return FALSE;
    }

    for (i = 0; i < numberOfEntries; i++)
    {
        INT itemIndex;
        PPH_STRING temp;
        SYSTEMTIME systemTime;

        if (entries[i].RevisionId == lastRevisionId)
        {
            PH_FORMAT format[2];

            PhInitFormatI64U(&format[0], lastRevisionId);
            format[0].Type |= FormatGroupDigits;
            PhInitFormatS(&format[1], L"/HEAD");
            temp = PhFormat(format, 2, 20);
        }
        else
        {
            temp = PhFormatUInt64(entries[i].RevisionId, TRUE);
        }

        itemIndex = PhAddListViewItem(BeRevisionListHandle, MAXINT, temp->Buffer, &entries[i]);
        PhDereferenceObject(temp);

        PhLargeIntegerToLocalSystemTime(&systemTime, &entries[i].TimeStamp);
        temp = PhFormatDateTime(&systemTime);
        PhSetListViewSubItem(BeRevisionListHandle, itemIndex, 1, temp->Buffer);
        PhDereferenceObject(temp);
    }

    if (BeRevisionInformation)
        PhFree(BeRevisionInformation);

    BeRevisionInformation = entries;

    ListView_SetItemState(BeRevisionListHandle, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ExtendedListView_SortItems(BeRevisionListHandle);

    return TRUE;
}

BOOLEAN BeSetCurrentRevision(
    _In_ ULONGLONG RevisionId
    )
{
    NTSTATUS status;
    ULONG i;
    ULONG count;

    if (RevisionId == BeCurrentRevision)
        return TRUE;

    if (RevisionId == 0)
    {
        BeCurrentRevision = 0;

        if (BeTempDatabaseHead)
        {
            DbCloseFile(BeTempDatabase, BeTempDatabaseHead);
            BeTempDatabaseHead = NULL;
        }

        if (BeTempDatabase)
        {
            DbCloseDatabase(BeTempDatabase);
            BeTempDatabase = NULL;
        }

        if (BeTempDatabaseFileName)
        {
            PhDeleteFileWin32(BeTempDatabaseFileName->Buffer);
            PhDereferenceObject(BeTempDatabaseFileName);
        }

        // Clear the root node.

        for (i = 0; i < BeRootNode->Children->Count; i++)
        {
            BeDestroyFileNode(BeRootNode->Children->Items[i]);
        }

        PhClearList(BeRootNode->Children);
        BeRootNode->Opened = FALSE;

        TreeNew_NodesStructured(BeFileListHandle);

        return TRUE;
    }

    BeSetCurrentRevision(0);

    SetCursor(LoadCursor(NULL, MAKEINTRESOURCE(IDC_WAIT)));

    BeCurrentRevision = RevisionId;
    BeTempDatabaseFileName = EnpFormatTempDatabaseFileName(BeConfig, FALSE);

    status = EnpCopyFileWin32(BeDatabaseFileName->Buffer, BeTempDatabaseFileName->Buffer, FILE_ATTRIBUTE_TEMPORARY, FALSE);

    if (!NT_SUCCESS(status))
    {
        PhShowStatus(BeWindowHandle, L"Unable to create the temporary database file", status, 0);
        BeSetCurrentRevision(0);
        return FALSE;
    }

    status = DbOpenDatabase(&BeTempDatabase, BeTempDatabaseFileName->Buffer, FALSE, 0);

    if (!NT_SUCCESS(status))
    {
        PhShowStatus(BeWindowHandle, L"Unable to open the temporary database", status, 0);
        BeSetCurrentRevision(0);
        return FALSE;
    }

    status = EnpMergeToHeadUntilRevision(BeTempDatabase, BeCurrentRevision, BeMessageHandler, &BeTempDatabaseHead);

    if (!NT_SUCCESS(status))
    {
        PhShowStatus(BeWindowHandle, L"Unable to merge to head", status, 0);
        BeSetCurrentRevision(0);
        return FALSE;
    }

    BeExpandFileNode(BeRootNode);
    TreeNew_NodesStructured(BeFileListHandle);

    if (BeSelectedFullPath)
        BeSelectFullPath(&BeSelectedFullPath->sr);

    count = ListView_GetItemCount(BeRevisionListHandle);

    for (i = 0; i < count; i++)
    {
        PEN_FILE_REVISION_INFORMATION revisionInfo;

        if (PhGetListViewItemParam(BeRevisionListHandle, i, &revisionInfo) && revisionInfo->RevisionId == RevisionId)
        {
            ListView_SetItemState(BeRevisionListHandle, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(BeRevisionListHandle, i, FALSE);
            break;
        }
    }

    SetCursor(LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW)));

    return TRUE;
}

#define SORT_FUNCTION(Column) BeFileListTreeNewCompare##Column

#define BEGIN_SORT_FUNCTION(Column) static int __cdecl BeFileListTreeNewCompare##Column( \
    _In_ void *_context, \
    _In_ const void *_elem1, \
    _In_ const void *_elem2 \
    ) \
{ \
    PBE_FILE_NODE node1 = *(PBE_FILE_NODE *)_elem1; \
    PBE_FILE_NODE node2 = *(PBE_FILE_NODE *)_elem2; \
    int sortResult = 0; \
    \
    if (node1->IsDirectory != node2->IsDirectory) \
        return node2->IsDirectory - node1->IsDirectory;

#define END_SORT_FUNCTION \
    if (sortResult == 0) \
        sortResult = PhCompareString(node1->Name, node2->Name, TRUE); \
    \
    return PhModifySort(sortResult, BeFileListSortOrder); \
}

BEGIN_SORT_FUNCTION(File)
{
    sortResult = PhCompareString(node1->Name, node2->Name, TRUE);
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

BEGIN_SORT_FUNCTION(Revision)
{
    sortResult = uint64cmp(node1->RevisionId, node2->RevisionId);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(TimeStamp)
{
    sortResult = uint64cmp(node1->TimeStamp.QuadPart, node2->TimeStamp.QuadPart);
}
END_SORT_FUNCTION

BOOLEAN BeFileListTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    )
{
    PBE_FILE_NODE node;

    switch (Message)
    {
    case TreeNewGetChildren:
        {
            PPH_TREENEW_GET_CHILDREN getChildren = Parameter1;

            node = (PBE_FILE_NODE)getChildren->Node;

            if (!node)
            {
                getChildren->Children = (PPH_TREENEW_NODE *)&BeRootNode;
                getChildren->NumberOfChildren = 1;
            }
            else
            {
                static PVOID sortFunctions[] =
                {
                    SORT_FUNCTION(File),
                    SORT_FUNCTION(Size),
                    SORT_FUNCTION(BackupTime),
                    SORT_FUNCTION(Revision),
                    SORT_FUNCTION(TimeStamp)
                };
                int (__cdecl *sortFunction)(void *, const void *, const void *);

                if (BeFileListSortColumn < BETNC_MAXIMUM)
                    sortFunction = sortFunctions[BeFileListSortColumn];
                else
                    sortFunction = NULL;

                if (sortFunction)
                {
                    qsort_s(node->Children->Items, node->Children->Count, sizeof(PVOID), sortFunction, NULL);
                }

                getChildren->Children = (PPH_TREENEW_NODE *)node->Children->Items;
                getChildren->NumberOfChildren = node->Children->Count;
            }
        }
        return TRUE;
    case TreeNewIsLeaf:
        {
            PPH_TREENEW_IS_LEAF isLeaf = Parameter1;

            node = (PBE_FILE_NODE)isLeaf->Node;
            isLeaf->IsLeaf = !node->HasChildren;
        }
        return TRUE;
    case TreeNewGetCellText:
        {
            PPH_TREENEW_GET_CELL_TEXT getCellText = Parameter1;

            node = (PBE_FILE_NODE)getCellText->Node;

            switch (getCellText->Id)
            {
            case BETNC_FILE:
                getCellText->Text = node->Name->sr;
                break;
            case BETNC_SIZE:
                getCellText->Text = PhGetStringRef(node->EndOfFileString);
                break;
            case BETNC_BACKUPTIME:
                getCellText->Text = PhGetStringRef(node->LastBackupTimeString);
                break;
            case BETNC_REVISION:
                getCellText->Text = PhGetStringRef(node->RevisionIdString);
                break;
            case BETNC_TIMESTAMP:
                getCellText->Text = PhGetStringRef(node->TimeStampString);
                break;
            default:
                return FALSE;
            }
        }
        return TRUE;
    case TreeNewGetNodeIcon:
        {
            PPH_TREENEW_GET_NODE_ICON getNodeIcon = Parameter1;
            PH_STRINGREF extension;

            node = (PBE_FILE_NODE)getNodeIcon->Node;

            if (node->IsDirectory)
            {
                extension = BeDirectoryIconKey;
            }
            else
            {
                PH_STRINGREF firstPart;

                PhSplitStringRefAtLastChar(&node->Name->sr, '.', &firstPart, &extension);
            }

            getNodeIcon->Icon = BeGetFileIconForExtension(&extension);
            getNodeIcon->Flags |= TN_CACHE_ICON;
        }
        return TRUE;
    case TreeNewSortChanged:
        {
            TreeNew_GetSort(hwnd, &BeFileListSortColumn, &BeFileListSortOrder);
            // Force a rebuild to sort the items.
            TreeNew_NodesStructured(hwnd);
        }
        return TRUE;
    case TreeNewSelectionChanged:
        {
            PBE_FILE_NODE selectedNode = BeGetSelectedFileNode();

            if (selectedNode)
                BeSelectedFullPath = BeComputeFullPath(selectedNode);
            else
                BeSelectedFullPath = NULL;
        }
        break;
    case TreeNewNodeExpanding:
        {
            node = Parameter1;

            if (!node->Opened)
            {
                BeExpandFileNode(node);
                TreeNew_NodesStructured(hwnd);
            }
        }
        return TRUE;
    case TreeNewKeyDown:
        {
            PPH_TREENEW_KEY_EVENT keyEvent = Parameter1;

            switch (keyEvent->VirtualKey)
            {
            case 'C':
                if (GetKeyState(VK_CONTROL) < 0)
                    SendMessage(BeWindowHandle, WM_COMMAND, ID_FILE_COPY, 0);
                break;
            case 'A':
                if (GetKeyState(VK_CONTROL) < 0)
                    TreeNew_SelectRange(hwnd, 0, -1);
                break;
            case VK_RETURN:
                SendMessage(BeWindowHandle, WM_COMMAND, ID_FILE_PREVIEW, 0);
                break;
            }
        }
        return TRUE;
    case TreeNewLeftDoubleClick:
        {
            SendMessage(BeWindowHandle, WM_COMMAND, ID_FILE_PREVIEW, 0);
        }
        break;
    case TreeNewContextMenu:
        {
            PPH_TREENEW_CONTEXT_MENU contextMenu = Parameter1;

            if (contextMenu->Node)
            {
                PPH_EMENU menu;
                PPH_EMENU_ITEM selectedItem;

                node = (PBE_FILE_NODE)contextMenu->Node;
                menu = PhCreateEMenu();
                PhLoadResourceEMenuItem(menu, BeInstanceHandle, MAKEINTRESOURCE(IDR_FILE), 0);
                PhSetFlagsEMenuItem(menu, ID_FILE_PREVIEW, PH_EMENU_DEFAULT, PH_EMENU_DEFAULT);

                if (node->IsDirectory)
                    PhEnableEMenuItem(menu, ID_FILE_PREVIEW, FALSE);

                if (BeGetSelectedFileNodeCount() != 1)
                {
                    PhSetFlagsAllEMenuItems(menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
                    PhEnableEMenuItem(menu, ID_FILE_COPY, TRUE);
                }

                selectedItem = PhShowEMenu(menu, hwnd, PH_EMENU_SHOW_LEFTRIGHT, PH_ALIGN_TOP | PH_ALIGN_LEFT, contextMenu->Location.x, contextMenu->Location.y);

                if (selectedItem)
                    SendMessage(BeWindowHandle, WM_COMMAND, selectedItem->Id, 0);

                PhDestroyEMenu(menu);
            }
        }
        break;
    }

    return FALSE;
}

PBE_FILE_NODE BeCreateFileNode(
    _In_opt_ PDB_FILE_DIRECTORY_INFORMATION Information,
    _In_opt_ PBE_FILE_NODE ParentNode
    )
{
    PBE_FILE_NODE node;

    node = PhAllocate(sizeof(BE_FILE_NODE));
    memset(node, 0, sizeof(BE_FILE_NODE));

    PhInitializeTreeNewNode(&node->Node);
    node->Node.Expanded = FALSE;

    node->Parent = ParentNode;
    node->Children = PhCreateList(1);

    if (Information)
    {
        SYSTEMTIME systemTime;

        node->Name = Information->FileName;
        PhReferenceObject(node->Name);

        node->TimeStamp = Information->TimeStamp;
        node->RevisionId = Information->RevisionId;
        node->EndOfFile = Information->EndOfFile;
        node->LastBackupTime = Information->LastBackupTime;

        if (!(Information->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        {
            node->EndOfFileString = PhFormatSize(node->EndOfFile.QuadPart, -1);
            node->RevisionIdString = PhFormatUInt64(node->RevisionId, TRUE);
        }

        if (node->TimeStamp.QuadPart != 0)
        {
            PhLargeIntegerToLocalSystemTime(&systemTime, &node->TimeStamp);
            node->TimeStampString = PhFormatDateTime(&systemTime);
        }

        if (node->LastBackupTime.QuadPart != 0)
        {
            PhLargeIntegerToLocalSystemTime(&systemTime, &node->LastBackupTime);
            node->LastBackupTimeString = PhFormatDateTime(&systemTime);
        }
    }

    if (ParentNode)
    {
        PhAddItemList(ParentNode->Children, node);
    }

    return node;
}

VOID BeDestroyFileNode(
    _In_ PBE_FILE_NODE Node
    )
{
    ULONG i;

    for (i = 0; i < Node->Children->Count; i++)
    {
        BeDestroyFileNode(Node->Children->Items[i]);
    }

    PhDereferenceObject(Node->Children);

    PhSwapReference(&Node->EndOfFileString, NULL);
    PhSwapReference(&Node->LastBackupTimeString, NULL);
    PhSwapReference(&Node->RevisionIdString, NULL);

    PhFree(Node);
}

BOOLEAN BeExpandFileNode(
    _In_ PBE_FILE_NODE Node
    )
{
    NTSTATUS status;
    PPH_STRING fullPath;
    PDBF_FILE directory;
    PDB_FILE_DIRECTORY_INFORMATION entries;
    ULONG numberOfEntries;

    if (Node->Opened || !Node->IsDirectory)
        return TRUE;

    Node->Opened = TRUE;

    fullPath = BeComputeFullPath(Node);
    status = DbCreateFile(BeTempDatabase, &fullPath->sr, BeTempDatabaseHead, 0, DB_FILE_OPEN, DB_FILE_DIRECTORY_FILE, NULL, &directory);

    if (!NT_SUCCESS(status))
        return FALSE;

    status = DbQueryDirectoryFile(BeTempDatabase, directory, &entries, &numberOfEntries);

    if (NT_SUCCESS(status))
    {
        ULONG i;

        for (i = 0; i < numberOfEntries; i++)
        {
            PBE_FILE_NODE childNode;

            childNode = BeCreateFileNode(&entries[i], Node);
            childNode->IsDirectory = !!(entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY);

            if (childNode->IsDirectory)
            {
                PDBF_FILE childDirectory;
                DB_FILE_STANDARD_INFORMATION standardInfo;

                if (NT_SUCCESS(DbCreateFile(BeTempDatabase, &entries[i].FileName->sr, directory, 0, DB_FILE_OPEN, DB_FILE_ATTRIBUTE_DIRECTORY, NULL, &childDirectory)))
                {
                    if (NT_SUCCESS(DbQueryInformationFile(BeTempDatabase, childDirectory, DbFileStandardInformation, &standardInfo, sizeof(DB_FILE_STANDARD_INFORMATION))))
                    {
                        if (standardInfo.NumberOfFiles != 0)
                            childNode->HasChildren = TRUE;
                    }

                    DbCloseFile(BeTempDatabase, childDirectory);
                }
            }
        }
    }

    DbFreeQueryDirectoryFile(entries, numberOfEntries);
    DbCloseFile(BeTempDatabase, directory);

    return NT_SUCCESS(status);
}

ULONG BeGetSelectedFileNodeCount(
    VOID
    )
{
    ULONG result;
    ULONG count;
    ULONG i;

    result = 0;
    count = TreeNew_GetFlatNodeCount(BeFileListHandle);

    for (i = 0; i < count; i++)
    {
        PBE_FILE_NODE node = (PBE_FILE_NODE)TreeNew_GetFlatNode(BeFileListHandle, i);

        if (node->Node.Selected)
            result++;
    }

    return result;
}

PBE_FILE_NODE BeGetSelectedFileNode(
    VOID
    )
{
    ULONG count;
    ULONG i;

    count = TreeNew_GetFlatNodeCount(BeFileListHandle);

    for (i = 0; i < count; i++)
    {
        PBE_FILE_NODE node = (PBE_FILE_NODE)TreeNew_GetFlatNode(BeFileListHandle, i);

        if (node->Node.Selected)
            return node;
    }

    return NULL;
}

PPH_STRING BeComputeFullPath(
    _In_ PBE_FILE_NODE Node
    )
{
    PPH_STRING result;
    SIZE_T totalLength;
    PBE_FILE_NODE currentNode;
    PCHAR currentPointer;

    totalLength = 0;
    currentNode = Node;

    while (currentNode && !currentNode->IsRoot)
    {
        totalLength += currentNode->Name->Length;

        if (currentNode->Parent)
            totalLength += sizeof(WCHAR);

        currentNode = currentNode->Parent;
    }

    result = PhCreateStringEx(NULL, totalLength);
    currentPointer = (PCHAR)result->Buffer + totalLength;
    currentNode = Node;

    while (currentNode && !currentNode->IsRoot)
    {
        currentPointer -= currentNode->Name->Length;
        memcpy(currentPointer, currentNode->Name->Buffer, currentNode->Name->Length);

        if (currentNode->Parent)
        {
            currentPointer -= sizeof(WCHAR);
            *(PWCHAR)currentPointer = '\\';
        }

        currentNode = currentNode->Parent;
    }

    return result;
}

VOID BeSelectFullPath(
    _In_ PPH_STRINGREF FullPath
    )
{
    PBE_FILE_NODE currentNode;
    PH_STRINGREF namePart;
    PH_STRINGREF remainingPart;

    currentNode = BeRootNode;
    remainingPart = *FullPath;

    // Remove trailing backslashes.
    while (remainingPart.Length != 0 && remainingPart.Buffer[remainingPart.Length / sizeof(WCHAR) - 1] == '\\')
        remainingPart.Length--;

    while (remainingPart.Length != 0 && currentNode)
    {
        PBE_FILE_NODE newNode;
        ULONG i;

        PhSplitStringRefAtChar(&remainingPart, '\\', &namePart, &remainingPart);

        if (namePart.Length == 0)
            continue;

        currentNode->Node.Expanded = TRUE;
        BeExpandFileNode(currentNode);

        newNode = NULL;

        for (i = 0; i < currentNode->Children->Count; i++)
        {
            PBE_FILE_NODE node = currentNode->Children->Items[i];

            if (PhEqualStringRef(&node->Name->sr, &namePart, TRUE))
            {
                newNode = node;
                break;
            }
        }

        currentNode = newNode;
    }

    TreeNew_NodesStructured(BeFileListHandle);

    if (remainingPart.Length == 0 && currentNode)
    {
        // We made it to the correct item, so select it.
        TreeNew_DeselectRange(BeFileListHandle, 0, -1);
        TreeNew_SelectRange(BeFileListHandle, currentNode->Node.Index, currentNode->Node.Index);
        TreeNew_EnsureVisible(BeFileListHandle, currentNode);
    }
}

BOOLEAN BeFileIconEntryCompareFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PBE_FILE_ICON_ENTRY entry1 = Entry1;
    PBE_FILE_ICON_ENTRY entry2 = Entry2;

    return PhEqualStringRef(&entry1->Key, &entry2->Key, FALSE);
}

ULONG BeFileIconEntryHashFunction(
    _In_ PVOID Entry
    )
{
    PBE_FILE_ICON_ENTRY entry = Entry;

    return PhHashBytes((PUCHAR)entry->Key.Buffer, entry->Key.Length);
}

HICON BeGetFileIconForExtension(
    _In_ PPH_STRINGREF Extension
    )
{
    static PH_STRINGREF dotString = PH_STRINGREF_INIT(L".");

    PPH_STRING upperExtension;
    BE_FILE_ICON_ENTRY entry;
    PBE_FILE_ICON_ENTRY realEntry;
    PPH_STRING extensionString;
    HICON icon;

    upperExtension = PhCreateStringEx(Extension->Buffer, Extension->Length);
    _wcsupr(upperExtension->Buffer);
    entry.Key = upperExtension->sr;

    realEntry = PhFindEntryHashtable(BeFileIconHashtable, &entry);

    if (realEntry)
    {
        PhDereferenceObject(upperExtension);
        return realEntry->Icon;
    }

    if (PhEqualStringRef(Extension, &BeDirectoryIconKey, FALSE))
    {
        SHFILEINFO fileInfo;

        icon = NULL;

        if (SHGetFileInfo(L"directory", FILE_ATTRIBUTE_DIRECTORY, &fileInfo, sizeof(SHFILEINFO), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
        {
            icon = fileInfo.hIcon;
        }
    }
    else
    {
        extensionString = PhConcatStringRef2(&dotString, Extension);

        if (Extension->Length != 0)
            icon = PhGetFileShellIcon(NULL, extensionString->Buffer, FALSE);
        else
            icon = PhGetFileShellIcon(NULL, L".no-extension-bkc-explorer", FALSE);

        PhDereferenceObject(extensionString);
    }

    if (!icon)
    {
        PhDereferenceObject(upperExtension);
        return NULL;
    }

    entry.Extension = upperExtension;
    entry.Key = upperExtension->sr;
    entry.Icon = icon;

    PhAddEntryHashtable(BeFileIconHashtable, &entry);

    return icon;
}

VOID BeDestroyRestoreParameters(
    _In_ PBE_RESTORE_PARAMETERS Parameters
    )
{
    PhSwapReference(&Parameters->FromFileName, NULL);
    PhSwapReference(&Parameters->ToDirectoryName, NULL);
    PhSwapReference(&Parameters->ToFileName, NULL);
    PhFree(Parameters);
}

VOID BePreviewSingleFileWithProgress(
    _In_ HWND ParentWindowHandle,
    _In_ ULONGLONG RevisionId,
    _In_ PPH_STRINGREF FileName
    )
{
    PBE_RESTORE_PARAMETERS parameters;

    parameters = PhAllocate(sizeof(BE_RESTORE_PARAMETERS));
    memset(parameters, 0, sizeof(BE_RESTORE_PARAMETERS));

    parameters->RevisionId = RevisionId;
    parameters->FromFileName = PhCreateStringEx(FileName->Buffer, FileName->Length);

    BeExecuteWithProgress(ParentWindowHandle, BePreviewSingleFileThreadStart, parameters);
}

NTSTATUS BePreviewSingleFileThreadStart(
    _In_ PVOID Parameter
    )
{
    static PH_STRINGREF backslash = PH_STRINGREF_INIT(L"\\");

    NTSTATUS status;
    PBE_RESTORE_PARAMETERS parameters = Parameter;
    PPH_STRING tempDirectoryName;
    PPH_STRING baseName;
    PPH_STRING fullName;

    tempDirectoryName = BeGetTempDirectoryName();

    if (!tempDirectoryName)
        goto Done;

    baseName = PhGetBaseName(parameters->FromFileName);
    CreateDirectory(tempDirectoryName->Buffer, NULL);
    status = EnRestoreFromRevision(BeConfig, EN_RESTORE_OVERWRITE_FILES, &parameters->FromFileName->sr, parameters->RevisionId, &tempDirectoryName->sr, &baseName->sr, BeMessageHandler);
    fullName = PhConcatStringRef3(&tempDirectoryName->sr, &backslash, &baseName->sr);

    if (NT_SUCCESS(status))
    {
        BeCompleteWithProgress();
        PhShellExecuteEx(BeWindowHandle, fullName->Buffer, NULL, SW_SHOW, 0, ULONG_MAX, NULL);
    }
    else
    {
        PhShowStatus(BeProgressWindowHandle, L"Unable to extract the file", status, 0);
    }

    PhDeleteFileWin32(fullName->Buffer);
    PhDeleteFileWin32(tempDirectoryName->Buffer);

    PhDereferenceObject(fullName);
    PhDereferenceObject(baseName);
    PhDereferenceObject(tempDirectoryName);

Done:
    BeDestroyRestoreParameters(parameters);
    BeCompleteWithProgress();

    return STATUS_SUCCESS;
}

VOID BeRestoreFileOrDirectoryWithProgress(
    _In_ HWND ParentWindowHandle,
    _In_ ULONGLONG RevisionId,
    _In_ PPH_STRINGREF FileName,
    _In_ BOOLEAN Directory
    )
{
    PPH_STRING fullPath;

    fullPath = PhCreateStringEx(FileName->Buffer, FileName->Length);

    if (Directory)
    {
        PVOID fileDialog = PhCreateOpenFileDialog();

        PhSetFileDialogOptions(fileDialog, PH_FILEDIALOG_PATHMUSTEXIST | PH_FILEDIALOG_PICKFOLDERS);

        if (PhShowFileDialog(ParentWindowHandle, fileDialog))
        {
            PPH_STRING fileName = PhGetFileDialogFileName(fileDialog);

            if (fileName)
            {
                NTSTATUS status;
                BOOLEAN empty;

                status = BeIsDirectoryEmpty(fileName->Buffer, &empty);

                if (NT_SUCCESS(status))
                {
                    if (empty || PhShowMessage(
                        ParentWindowHandle,
                        MB_ICONWARNING | MB_YESNO,
                        L"The destination directory is not empty, and its contents may be overwritten. Do you want to continue?"
                        ) == IDYES)
                    {
                        PBE_RESTORE_PARAMETERS parameters;

                        parameters = PhAllocate(sizeof(BE_RESTORE_PARAMETERS));
                        parameters->RevisionId = RevisionId;
                        parameters->FromFileName = fullPath;
                        PhReferenceObject(fullPath);
                        parameters->ToDirectoryName = fileName;
                        PhReferenceObject(fileName);
                        parameters->ToFileName = NULL;

                        BeExecuteWithProgress(ParentWindowHandle, BeRestoreFileOrDirectoryThreadStart, parameters);
                    }
                }
                else
                {
                    PhShowStatus(ParentWindowHandle, L"Unable to open the destination directory", status, 0);
                }

                PhDereferenceObject(fileName);
            }
        }

        PhFreeFileDialog(fileDialog);
    }
    else
    {
        static PH_FILETYPE_FILTER filters[] =
        {
            { L"All files (*.*)", L"*.*" }
        };

        PPH_STRING baseName = PhGetBaseName(fullPath);
        PVOID fileDialog = PhCreateSaveFileDialog();

        PhSetFileDialogFilter(fileDialog, filters, sizeof(filters) / sizeof(PH_FILETYPE_FILTER));
        PhSetFileDialogFileName(fileDialog, baseName->Buffer);

        if (PhShowFileDialog(ParentWindowHandle, fileDialog))
        {
            PPH_STRING fileName = PhGetFileDialogFileName(fileDialog);

            if (fileName)
            {
                PH_STRINGREF directoryPart;
                PH_STRINGREF filePart;

                if (PhSplitStringRefAtLastChar(&fileName->sr, '\\', &directoryPart, &filePart))
                {
                    PBE_RESTORE_PARAMETERS parameters;

                    parameters = PhAllocate(sizeof(BE_RESTORE_PARAMETERS));
                    parameters->RevisionId = RevisionId;
                    parameters->FromFileName = fullPath;
                    PhReferenceObject(fullPath);
                    parameters->ToDirectoryName = PhCreateStringEx(directoryPart.Buffer, directoryPart.Length);
                    parameters->ToFileName = PhCreateStringEx(filePart.Buffer, filePart.Length);

                    BeExecuteWithProgress(ParentWindowHandle, BeRestoreFileOrDirectoryThreadStart, parameters);
                }

                PhDereferenceObject(fileName);
            }
        }

        PhFreeFileDialog(fileDialog);
        PhDereferenceObject(baseName);
    }

    PhDereferenceObject(fullPath);
}

NTSTATUS BeRestoreFileOrDirectoryThreadStart(
    _In_ PVOID Parameter
    )
{
    NTSTATUS status;
    PBE_RESTORE_PARAMETERS parameters = Parameter;

    status = EnRestoreFromRevision(
        BeConfig,
        EN_RESTORE_OVERWRITE_FILES,
        &parameters->FromFileName->sr,
        parameters->RevisionId,
        &parameters->ToDirectoryName->sr,
        parameters->ToFileName ? &parameters->ToFileName->sr : NULL,
        BeMessageHandler
        );

    if (!NT_SUCCESS(status))
        PhShowStatus(BeProgressWindowHandle, L"Unable to restore the file or directory", status, 0);

    BeDestroyRestoreParameters(parameters);
    BeCompleteWithProgress();

    return STATUS_SUCCESS;
}

PPH_STRING BePromptForConfigFileName(
    VOID
    )
{
    static PH_FILETYPE_FILTER filters[] =
    {
        { L"Configuration files (*.ini)", L"*.ini" },
        { L"All files (*.*)", L"*.*" }
    };

    PVOID fileDialog;
    PPH_STRING fileName = NULL;

    fileDialog = PhCreateOpenFileDialog();
    PhSetFileDialogFilter(fileDialog, filters, sizeof(filters) / sizeof(PH_FILETYPE_FILTER));

    if (PhShowFileDialog(BeWindowHandle, fileDialog))
    {
        fileName = PhGetFileDialogFileName(fileDialog);
    }

    PhFreeFileDialog(fileDialog);

    return fileName;
}

VOID BeMessageHandler(
    _In_ ULONG Level,
    _In_ _Assume_refs_(1) PPH_STRING Message
    )
{
    PH_STRING_BUILDER sb;
    PPH_STRING now;

    if (Level == EN_MESSAGE_PROGRESS)
    {
        if (PhStartsWithString2(Message, L"Compressing: ", FALSE) || PhStartsWithString2(Message, L"Extracting: ", FALSE))
        {
            if (BeProgressWindowHandle)
                SendMessage(BeProgressWindowHandle, BE_PROGRESS_MESSAGE_UPDATE, BeGetProgressFromMessage(&Message->sr), 100);

            return;
        }
    }

    PhInitializeStringBuilder(&sb, Message->Length);

    now = PhFormatDateTime(NULL);
    PhAppendStringBuilder(&sb, &now->sr);
    PhDereferenceObject(now);
    PhAppendStringBuilder2(&sb, L": ");

    if (Level == EN_MESSAGE_WARNING)
        PhAppendStringBuilder2(&sb, L"** Warning ** ");
    if (Level == EN_MESSAGE_ERROR)
        PhAppendStringBuilder2(&sb, L"** ERROR ** ");

    PhAppendStringBuilder(&sb, &Message->sr);
    PhDereferenceObject(Message);

    ListBox_AddString(BeLogHandle, sb.String->Buffer);
    ListBox_SetTopIndex(BeLogHandle, ListBox_GetCount(BeLogHandle) - 1);

    PhDeleteStringBuilder(&sb);
}

ULONG BeGetProgressFromMessage(
    _In_ PPH_STRINGREF Message
    )
{
    static PH_STRINGREF colonSeparator = PH_STRINGREF_INIT(L": ");

    PH_STRINGREF firstPart;
    PH_STRINGREF secondPart;
    ULONG64 integer;

    PhSplitStringRefAtString(Message, &colonSeparator, FALSE, &firstPart, &secondPart);
    PhSplitStringRefAtChar(&secondPart, '.', &firstPart, &secondPart);

    while (firstPart.Length != 0 && *firstPart.Buffer == ' ')
    {
        firstPart.Buffer++;
        firstPart.Length -= sizeof(WCHAR);
    }

    PhStringToInteger64(&firstPart, 10, &integer);

    return (ULONG)integer;
}

BOOLEAN BeExecuteWithProgress(
    _In_ HWND ParentWindowHandle,
    _In_ PUSER_THREAD_START_ROUTINE ThreadStart,
    _In_opt_ PVOID Context
    )
{
    HANDLE threadHandle;

    if (BeProgressWindowHandle)
    {
        PhShowError(ParentWindowHandle, L"Another operation is currently in progress. You must first wait for that operation to complete.");
        return FALSE;
    }

    BeProgressWindowHandle = BeCreateProgressDialog(ParentWindowHandle);
    threadHandle = PhCreateThread(0, ThreadStart, Context);

    if (threadHandle)
    {
        NtClose(threadHandle);
    }
    else
    {
        SendMessage(BeProgressWindowHandle, BE_PROGRESS_MESSAGE_CLOSE, 0, 0);
        BeProgressWindowHandle = NULL;
        return FALSE;
    }

    return TRUE;
}

VOID BeCompleteWithProgress(
    VOID
    )
{
    if (BeProgressWindowHandle)
    {
        SendMessage(BeProgressWindowHandle, BE_PROGRESS_MESSAGE_CLOSE, 0, 0);
        BeProgressWindowHandle = NULL;
    }
}

PPH_STRING BeGetTempDirectoryName(
    VOID
    )
{
    WCHAR tempNameBuffer[18];
    PH_STRINGREF tempNameSr;
    WCHAR tempPathBuffer[MAX_PATH + 1];
    PH_STRINGREF tempPathSr;

    tempNameBuffer[0] = 'b';
    tempNameBuffer[1] = 'k';
    tempNameBuffer[2] = 'e';
    tempNameBuffer[3] = 'x';
    tempNameBuffer[4] = 'p';
    tempNameBuffer[5] = '.';
    PhGenerateRandomAlphaString(tempNameBuffer + 6, 9);
    tempNameBuffer[14] = '.';
    tempNameBuffer[15] = 't';
    tempNameBuffer[16] = 'm';
    tempNameBuffer[17] = 'p';
    tempNameSr.Buffer = tempNameBuffer;
    tempNameSr.Length = 18 * sizeof(WCHAR);

    if (GetTempPath(MAX_PATH + 1, tempPathBuffer) != 0)
    {
        PhInitializeStringRef(&tempPathSr, tempPathBuffer);

        return PhConcatStringRef2(&tempPathSr, &tempNameSr);
    }

    return NULL;
}

BOOLEAN BeIsDirectoryEmptyEnumCallback(
    _In_ PFILE_DIRECTORY_INFORMATION Information,
    _In_opt_ PVOID Context
    )
{
    if (Information->FileNameLength == sizeof(WCHAR) && Information->FileName[0] == '.')
        return TRUE;
    if (Information->FileNameLength == 2 * sizeof(WCHAR) && Information->FileName[0] == '.' && Information->FileName[1] == '.')
        return TRUE;

    *(PBOOLEAN)Context = FALSE;

    return FALSE;
}

NTSTATUS BeIsDirectoryEmpty(
    _In_ PWSTR DirectoryName,
    _Out_ PBOOLEAN Empty
    )
{
    NTSTATUS status;
    BOOLEAN empty = TRUE;
    HANDLE directoryHandle;

    status = PhCreateFileWin32(&directoryHandle, DirectoryName, FILE_GENERIC_READ, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);

    if (!NT_SUCCESS(status))
        return status;

    status = PhEnumDirectoryFile(directoryHandle, NULL, BeIsDirectoryEmptyEnumCallback, &empty);
    NtClose(directoryHandle);

    if (NT_SUCCESS(status))
        *Empty = empty;

    return status;
}

// Copied from appsup.c
BOOLEAN PhGetListViewContextMenuPoint(
    _In_ HWND ListViewHandle,
    _Out_ PPOINT Point
    )
{
    static PH_INTEGER_PAIR PhSmallIconSize = { 16, 16 };
    static PH_INITONCE initOnce;

    INT selectedIndex;
    RECT bounds;
    RECT clientRect;

    if (PhBeginInitOnce(&initOnce))
    {
        PhSmallIconSize.X = GetSystemMetrics(SM_CXSMICON);
        PhSmallIconSize.Y = GetSystemMetrics(SM_CYSMICON);
        PhEndInitOnce(&initOnce);
    }

    // The user pressed a key to display the context menu.
    // Suggest where the context menu should display.

    if ((selectedIndex = ListView_GetNextItem(ListViewHandle, -1, LVNI_SELECTED)) != -1)
    {
        if (ListView_GetItemRect(ListViewHandle, selectedIndex, &bounds, LVIR_BOUNDS))
        {
            Point->x = bounds.left + PhSmallIconSize.X / 2;
            Point->y = bounds.top + PhSmallIconSize.Y / 2;

            GetClientRect(ListViewHandle, &clientRect);

            if (Point->x < 0 || Point->y < 0 || Point->x >= clientRect.right || Point->y >= clientRect.bottom)
            {
                // The menu is going to be outside of the control. Just put it at the top-left.
                Point->x = 0;
                Point->y = 0;
            }

            ClientToScreen(ListViewHandle, Point);

            return TRUE;
        }
    }

    Point->x = 0;
    Point->y = 0;
    ClientToScreen(ListViewHandle, Point);

    return FALSE;
}
