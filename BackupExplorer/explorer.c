#include "explorer.h"
#include <windowsx.h>
#include "../Backup/config.h"
#include "../Backup/engine.h"

#define BETNC_FILE 0
#define BETNC_SIZE 1
#define BETNC_BACKUPTIME 2

HWND BeWindowHandle;
HWND BeRevisionListHandle;
HWND BeFileListHandle;
HWND BeLogHandle;
PPH_STRING BeConfigFileName;
PBK_CONFIG BeConfig;

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

VOID BeConsoleMessageHandler(
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
            PH_STRINGREF emptySr;
            ULONGLONG lastRevisionId;
            PEN_FILE_REVISION_INFORMATION entries;
            ULONG numberOfEntries;
            ULONG i;

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

            PhAddTreeNewColumn(BeFileListHandle, BETNC_FILE, TRUE, L"File", 200, PH_ALIGN_LEFT, -2, 0);
            PhAddTreeNewColumn(BeFileListHandle, BETNC_SIZE, TRUE, L"Size", 80, PH_ALIGN_RIGHT, 0, DT_RIGHT);
            PhAddTreeNewColumn(BeFileListHandle, BETNC_BACKUPTIME, TRUE, L"Last Backup Time", 140, PH_ALIGN_LEFT, 1, 0);
            TreeNew_SetSort(BeFileListHandle, BETNC_FILE, AscendingSortOrder);

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

            // Get the list of revisions.

            PhInitializeEmptyStringRef(&emptySr);

            status = EnQueryRevision(BeConfig, BeConsoleMessageHandler, &lastRevisionId, NULL, NULL, NULL);

            if (NT_SUCCESS(status))
                status = EnQueryFileRevisions(BeConfig, &emptySr, BeConsoleMessageHandler, &entries, &numberOfEntries);

            if (!NT_SUCCESS(status))
            {
                PhShowStatus(hwndDlg, L"Unable to read list of revisions", status, 0);
                break;
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
        }
        break;
    case WM_DESTROY:
        {
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
