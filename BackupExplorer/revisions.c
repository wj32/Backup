#include "explorer.h"
#include <emenu.h>

typedef struct _BE_REVISIONS_CONTEXT
{
    PPH_STRING FileName;
    HWND ListHandle;
    PEN_FILE_REVISION_INFORMATION Information;
} BE_REVISIONS_CONTEXT, *PBE_REVISIONS_CONTEXT;

INT_PTR CALLBACK BeRevisionsDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    );

VOID BeShowRevisionsDialog(
    __in HWND ParentWindowHandle,
    __in PPH_STRINGREF FileName
    )
{
    BE_REVISIONS_CONTEXT context;

    memset(&context, 0, sizeof(BE_REVISIONS_CONTEXT));
    context.FileName = PhCreateStringEx(FileName->Buffer, FileName->Length);

    DialogBoxParam(
        BeInstanceHandle,
        MAKEINTRESOURCE(IDD_REVISIONS),
        ParentWindowHandle,
        BeRevisionsDlgProc,
        (LPARAM)&context
        );

    PhDereferenceObject(context.FileName);

    if (context.Information)
        PhFree(context.Information);
}

VOID BeLoadFileRevisions(
    __in HWND hwndDlg,
    __in PBE_REVISIONS_CONTEXT Context
    )
{
    NTSTATUS status;
    ULONGLONG lastRevisionId;
    PEN_FILE_REVISION_INFORMATION entries;
    ULONG numberOfEntries;
    ULONG i;

    status = EnQueryRevision(BeConfig, BeMessageHandler, &lastRevisionId, NULL, NULL, NULL);

    if (!NT_SUCCESS(status))
    {
        PhShowStatus(hwndDlg, L"Unable to query database", status, 0);
    }

    status = EnQueryFileRevisions(BeConfig, &Context->FileName->sr, BeMessageHandler, &entries, &numberOfEntries);

    if (!NT_SUCCESS(status))
    {
        PhShowStatus(hwndDlg, L"Unable to query file revisions", status, 0);
        return;
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

        itemIndex = PhAddListViewItem(Context->ListHandle, MAXINT, temp->Buffer, &entries[i]);
        PhDereferenceObject(temp);

        if (!(entries[i].Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
        {
            temp = PhFormatSize(entries[i].EndOfFile.QuadPart, -1);
            PhSetListViewSubItem(Context->ListHandle, itemIndex, 1, temp->Buffer);
            PhDereferenceObject(temp);
        }

        PhLargeIntegerToLocalSystemTime(&systemTime, &entries[i].TimeStamp);
        temp = PhFormatDateTime(&systemTime);
        PhSetListViewSubItem(Context->ListHandle, itemIndex, 2, temp->Buffer);
        PhDereferenceObject(temp);
    }

    if (Context->Information)
        PhFree(Context->Information);

    Context->Information = entries;
}

INT_PTR CALLBACK BeRevisionsDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    PBE_REVISIONS_CONTEXT context;

    context = (PBE_REVISIONS_CONTEXT)GetProp(hwndDlg, L"Context");

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context = (PBE_REVISIONS_CONTEXT)lParam;
            SetProp(hwndDlg, L"Context", (HANDLE)context);

            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            context->ListHandle = GetDlgItem(hwndDlg, IDC_LIST);
            PhSetListViewStyle(context->ListHandle, FALSE, TRUE);
            PhSetControlTheme(context->ListHandle, L"explorer");

            PhAddListViewColumn(context->ListHandle, 0, 0, 0, LVCFMT_LEFT, 55, L"Revision");
            PhAddListViewColumn(context->ListHandle, 1, 1, 1, LVCFMT_RIGHT, 80, L"Size");
            PhAddListViewColumn(context->ListHandle, 2, 2, 2, LVCFMT_LEFT, 140, L"Time Stamp");

            BeLoadFileRevisions(hwndDlg, context);
        }
        break;
    case WM_DESTROY:
        {
            RemoveProp(hwndDlg, L"Context");
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDCANCEL:
            case IDOK:
                EndDialog(hwndDlg, IDOK);
                break;
            case ID_FILE_PREVIEW:
                {
                    PEN_FILE_REVISION_INFORMATION revisionInfo;

                    revisionInfo = PhGetSelectedListViewItemParam(context->ListHandle);

                    if (revisionInfo)
                    {
                        if (!(revisionInfo->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY))
                            BePreviewSingleFileWithProgress(hwndDlg, revisionInfo->RevisionId, &context->FileName->sr);
                    }
                }
                break;
            case ID_FILE_RESTORE:
                {
                    PEN_FILE_REVISION_INFORMATION revisionInfo;

                    revisionInfo = PhGetSelectedListViewItemParam(context->ListHandle);

                    if (revisionInfo)
                    {
                        BeRestoreFileOrDirectoryWithProgress(hwndDlg, revisionInfo->RevisionId, &context->FileName->sr, !!(revisionInfo->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY));
                    }
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
            case NM_DBLCLK:
                {
                    if (header->hwndFrom == context->ListHandle)
                    {
                        SendMessage(hwndDlg, WM_COMMAND, ID_FILE_PREVIEW, 0);
                    }
                }
                break;
            }
        }
        break;
    case WM_CONTEXTMENU:
        {
            if ((HWND)wParam == context->ListHandle)
            {
                POINT point;
                PEN_FILE_REVISION_INFORMATION revisionInfo;
                PPH_EMENU menu;
                INT selectedCount;
                PPH_EMENU_ITEM selectedItem;

                point.x = (SHORT)LOWORD(lParam);
                point.y = (SHORT)HIWORD(lParam);

                if (point.x == -1 && point.y == -1)
                    PhGetListViewContextMenuPoint((HWND)wParam, &point);

                selectedCount = ListView_GetSelectedCount(context->ListHandle);
                revisionInfo = PhGetSelectedListViewItemParam(context->ListHandle);

                if (selectedCount != 0)
                {
                    menu = PhCreateEMenu();
                    PhLoadResourceEMenuItem(menu, BeInstanceHandle, MAKEINTRESOURCE(IDR_FILE), 0);

                    while (menu->Items->Count > 2)
                        PhDestroyEMenuItem(menu->Items->Items[2]);

                    PhSetFlagsEMenuItem(menu, ID_FILE_PREVIEW, PH_EMENU_DEFAULT, PH_EMENU_DEFAULT);

                    if (revisionInfo->Attributes & DB_FILE_ATTRIBUTE_DIRECTORY)
                        PhSetFlagsEMenuItem(menu, ID_FILE_PREVIEW, PH_EMENU_DISABLED, PH_EMENU_DISABLED);

                    selectedItem = PhShowEMenu(menu, context->ListHandle, PH_EMENU_SHOW_LEFTRIGHT | PH_EMENU_SHOW_NONOTIFY,
                        PH_ALIGN_LEFT | PH_ALIGN_TOP, point.x, point.y);

                    if (selectedItem)
                        SendMessage(hwndDlg, WM_COMMAND, selectedItem->Id, 0);

                    PhDestroyEMenu(menu);
                }
            }
        }
        break;
    }

    return FALSE;
}