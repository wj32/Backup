/*
 * Backup Explorer -
 *   progress dialog
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

INT_PTR CALLBACK BeProgressDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

HWND BeCreateProgressDialog(
    _In_ HWND ParentWindowHandle
    )
{
    HWND windowHandle;

    EnableWindow(ParentWindowHandle, FALSE);

    windowHandle = CreateDialog(
        BeInstanceHandle,
        MAKEINTRESOURCE(IDD_PROGRESS),
        ParentWindowHandle,
        BeProgressDlgProc
        );
    PhCenterWindow(windowHandle, ParentWindowHandle);
    ShowWindow(windowHandle, SW_SHOW);

    return windowHandle;
}

INT_PTR CALLBACK BeProgressDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    switch (uMsg)
    {
    case BE_PROGRESS_MESSAGE_CLOSE:
        EnableWindow(GetParent(hwndDlg), TRUE);
        DestroyWindow(hwndDlg);
        break;
    case BE_PROGRESS_MESSAGE_UPDATE:
        {
            HWND progressHandle = GetDlgItem(hwndDlg, IDC_PROGRESS);

            SendMessage(progressHandle, PBM_SETRANGE, 0, MAKELPARAM(0, lParam));
            SendMessage(progressHandle, PBM_SETPOS, wParam, 0);
        }
        break;
    }

    return FALSE;
}
