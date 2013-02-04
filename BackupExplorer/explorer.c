#include "explorer.h"
#include <windowsx.h>
#include "../Backup/config.h"
#include "../Backup/db.h"
#include "../Backup/vssobj.h"
#include "../Backup/package.h"
#include "../Backup/engine.h"
#include "../Backup/enginep.h"
#include "explorerp.h"

HWND BeWindowHandle;
HWND BeRevisionListHandle;

PPH_STRING BeDatabaseFileName;
ULONGLONG BeCurrentRevision;
PPH_STRING BeTempDatabaseFileName;
PDB_DATABASE BeTempDatabase;
PDBF_FILE BeTempDatabaseHead;

HWND BeFileListHandle;
ULONG BeFileListSortColumn;
PH_SORT_ORDER BeFileListSortOrder;
PBE_FILE_NODE BeRootNode;

HWND BeLogHandle;
PPH_STRING BeConfigFileName;
PBK_CONFIG BeConfig;

PPH_HASHTABLE BeFileIconHashtable;
PH_STRINGREF BeDirectoryIconKey = PH_STRINGREF_INIT(L"\\");

INT_PTR CALLBACK BeExplorerDlgProc(
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
            NTSTATUS status;
            PH_STRINGREF databaseName;

            BeWindowHandle = hwndDlg;
            BeRevisionListHandle = GetDlgItem(hwndDlg, IDC_REVISIONS);
            BeFileListHandle = GetDlgItem(hwndDlg, IDC_FILES);
            BeLogHandle = GetDlgItem(hwndDlg, IDC_LOG);

            PhCenterWindow(BeWindowHandle, NULL);

            // Revision list

            PhSetListViewStyle(BeRevisionListHandle, FALSE, TRUE);
            PhSetControlTheme(BeRevisionListHandle, L"explorer");
            PhSetExtendedListView(BeRevisionListHandle);
            PhAddListViewColumn(BeRevisionListHandle, 0, 0, 0, LVCFMT_RIGHT, 55, L"Revision");
            PhAddListViewColumn(BeRevisionListHandle, 1, 1, 1, LVCFMT_LEFT, 140, L"Time");
            ExtendedListView_AddFallbackColumn(BeRevisionListHandle, 0);
            ExtendedListView_SetSort(BeRevisionListHandle, 0, DescendingSortOrder);

            // File list

            PhSetControlTheme(BeFileListHandle, L"explorer");
            PhAddTreeNewColumn(BeFileListHandle, BETNC_FILE, TRUE, L"File", 200, PH_ALIGN_LEFT, -2, 0);
            PhAddTreeNewColumn(BeFileListHandle, BETNC_SIZE, TRUE, L"Size", 80, PH_ALIGN_RIGHT, 0, DT_RIGHT);
            PhAddTreeNewColumn(BeFileListHandle, BETNC_BACKUPTIME, TRUE, L"Last Backup Time", 140, PH_ALIGN_LEFT, 1, 0);
            TreeNew_SetSort(BeFileListHandle, BETNC_FILE, AscendingSortOrder);
            TreeNew_SetCallback(BeFileListHandle, BeFileListTreeNewCallback, NULL);
            TreeNew_SetExtendedFlags(BeFileListHandle, TN_FLAG_ITEM_DRAG_SELECT, TN_FLAG_ITEM_DRAG_SELECT);

            BeRootNode = BeCreateFileNode(NULL, NULL);
            BeRootNode->Node.Expanded = TRUE;
            BeRootNode->IsDirectory = TRUE;
            BeRootNode->HasChildren = TRUE;
            BeRootNode->Name = PhCreateString(L"\\");

            // File icons

            BeFileIconHashtable = PhCreateHashtable(sizeof(BE_FILE_ICON_ENTRY), BeFileIconEntryCompareFunction, BeFileIconEntryHashFunction, 4);

            // Open the configuration file.

            BeConfigFileName = BePromptForConfigFileName();

            if (!BeConfigFileName)
            {
                EndDialog(hwndDlg, IDCANCEL);
                break;
            }

            if (!NT_SUCCESS(status = BkCreateConfigFromFile(BeConfigFileName->Buffer, &BeConfig)) || !BeConfig->DestinationDirectory)
            {
                if (!BeConfig->DestinationDirectory)
                    status = STATUS_INVALID_PARAMETER;

                PhShowStatus(hwndDlg, L"Unable to read configuration file", status, 0);
                EndDialog(hwndDlg, IDCANCEL);
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
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDCANCEL:
                EndDialog(hwndDlg, IDCANCEL);
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

        itemIndex = PhAddListViewItem(BeRevisionListHandle, MAXINT, temp->Buffer, NULL);
        PhDereferenceObject(temp);

        PhLargeIntegerToLocalSystemTime(&systemTime, &entries[i].TimeStamp);
        temp = PhFormatDateTime(&systemTime);
        PhSetListViewSubItem(BeRevisionListHandle, itemIndex, 1, temp->Buffer);
        PhDereferenceObject(temp);
    }

    PhFree(entries);

    ListView_SetItemState(BeRevisionListHandle, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ExtendedListView_SortItems(BeRevisionListHandle);

    BeSetCurrentRevision(lastRevisionId);

    return TRUE;
}

BOOLEAN BeSetCurrentRevision(
    __in ULONGLONG RevisionId
    )
{
    NTSTATUS status;

    if (RevisionId == 0)
    {
        ULONG i;

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

        return TRUE;
    }

    BeSetCurrentRevision(0);

    SetCursor(LoadCursor(NULL, MAKEINTRESOURCE(IDC_WAIT)));

    BeCurrentRevision = RevisionId;
    BeTempDatabaseFileName = EnpFormatTempDatabaseFileName(BeConfig);

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

    SetCursor(LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW)));

    return TRUE;
}

BOOLEAN BeFileListTreeNewCallback(
    __in HWND hwnd,
    __in PH_TREENEW_MESSAGE Message,
    __in_opt PVOID Parameter1,
    __in_opt PVOID Parameter2,
    __in_opt PVOID Context
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
            default:
                return FALSE;
            }

            getCellText->Flags = TN_CACHE;
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
    }

    return FALSE;
}

PBE_FILE_NODE BeCreateFileNode(
    __in_opt PDB_FILE_DIRECTORY_INFORMATION Information,
    __in_opt PBE_FILE_NODE ParentNode
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
        node->Name = Information->FileName;
        PhReferenceObject(node->Name);

        node->TimeStamp = Information->TimeStamp;
        node->RevisionId = Information->RevisionId;
        node->EndOfFile = Information->EndOfFile;
        node->LastBackupTime = Information->LastBackupTime;
    }

    if (ParentNode)
    {
        PhAddItemList(ParentNode->Children, node);
    }

    return node;
}

VOID BeDestroyFileNode(
    __in PBE_FILE_NODE Node
    )
{
    ULONG i;

    for (i = 0; i < Node->Children->Count; i++)
    {
        BeDestroyFileNode(Node->Children->Items[i]);
    }

    PhDereferenceObject(Node->Children);
    PhFree(Node);
}

BOOLEAN BeExpandFileNode(
    __in PBE_FILE_NODE Node
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

PPH_STRING BeComputeFullPath(
    __in PBE_FILE_NODE Node
    )
{
    PPH_STRING result;
    SIZE_T totalLength;
    PBE_FILE_NODE currentNode;
    PCHAR currentPointer;

    totalLength = 0;
    currentNode = Node;

    while (currentNode)
    {
        totalLength += currentNode->Name->Length;

        if (currentNode->Parent)
            totalLength += sizeof(WCHAR);

        currentNode = currentNode->Parent;
    }

    result = PhCreateStringEx(NULL, totalLength);
    currentPointer = (PCHAR)result->Buffer + totalLength;
    currentNode = Node;

    while (currentNode)
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

BOOLEAN BeFileIconEntryCompareFunction(
    __in PVOID Entry1,
    __in PVOID Entry2
    )
{
    PBE_FILE_ICON_ENTRY entry1 = Entry1;
    PBE_FILE_ICON_ENTRY entry2 = Entry2;

    return PhEqualStringRef(&entry1->Key, &entry2->Key, FALSE);
}

ULONG BeFileIconEntryHashFunction(
    __in PVOID Entry
    )
{
    PBE_FILE_ICON_ENTRY entry = Entry;

    return PhHashBytes((PUCHAR)entry->Key.Buffer, entry->Key.Length);
}

HICON BeGetFileIconForExtension(
    __in PPH_STRINGREF Extension
    )
{
    static PH_STRINGREF dotString = PH_STRINGREF_INIT(L".");

    PPH_STRING upperExtension;
    BE_FILE_ICON_ENTRY entry;
    PBE_FILE_ICON_ENTRY realEntry;
    PPH_STRING extensionString;
    HICON icon;

    upperExtension = PhCreateStringEx(Extension->Buffer, Extension->Length);
    PhUpperString(upperExtension);
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
        icon = PhGetFileShellIcon(NULL, extensionString->Buffer, FALSE);
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
    __in ULONG Level,
    __in __assumeRefs(1) PPH_STRING Message
    )
{
    PH_STRING_BUILDER sb;

    if (Level == EN_MESSAGE_PROGRESS)
        return;

    PhInitializeStringBuilder(&sb, Message->Length);

    if (Level == EN_MESSAGE_WARNING)
        PhAppendStringBuilder2(&sb, L"** Warning ** ");
    if (Level == EN_MESSAGE_ERROR)
        PhAppendStringBuilder2(&sb, L"** ERROR ** ");

    PhAppendStringBuilder(&sb, Message);
    PhDereferenceObject(Message);

    ListBox_AddString(BeLogHandle, sb.String->Buffer);
    ListBox_SetTopIndex(BeLogHandle, ListBox_GetCount(BeLogHandle) - 1);

    PhDeleteStringBuilder(&sb);
}
