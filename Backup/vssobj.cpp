/*
 * Backup -
 *   VSS interface
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
#include "vssobj.h"
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <vector>
#include <algorithm>

typedef HRESULT (STDAPICALLTYPE *_CreateVssBackupComponentsInternal)(
    _Out_ IVssBackupComponents **ppBackup
    );

typedef void (APIENTRY *_VssFreeSnapshotPropertiesInternal)(
    _In_ VSS_SNAPSHOT_PROP *pProp
    );

typedef struct _BK_VSS_SNAPSHOT
{
    VSS_ID SnapshotId;
    PPH_STRING Volume;
    PH_STRINGREF VolumeSr; // without trailing backslash
    PPH_STRING SnapshotDevice;
} BK_VSS_SNAPSHOT, *PBK_VSS_SNAPSHOT;

typedef struct _BK_VSS_OBJECT
{
    IVssBackupComponents *Object;
    VSS_ID SnapshotSetId;
    std::vector<BK_VSS_SNAPSHOT> Snapshots;
} BK_VSS_OBJECT, *PBK_VSS_OBJECT;

static _CreateVssBackupComponentsInternal CreateVssBackupComponentsInternal_I;
static _VssFreeSnapshotPropertiesInternal VssFreeSnapshotPropertiesInternal_I;

HRESULT BkCreateVssObject(
    _Out_ PBK_VSS_OBJECT *Vss
    )
{
    static PH_INITONCE initOnce = PH_INITONCE_INIT;
    static HMODULE vssapiBase;

    HRESULT result;
    PBK_VSS_OBJECT vss;
    IVssBackupComponents *backupComponents;

    if (PhBeginInitOnce(&initOnce))
    {
        vssapiBase = LoadLibrary(L"vssapi.dll");

        if (vssapiBase)
        {
            CreateVssBackupComponentsInternal_I = (_CreateVssBackupComponentsInternal)GetProcAddress(vssapiBase, "CreateVssBackupComponentsInternal");
            VssFreeSnapshotPropertiesInternal_I = (_VssFreeSnapshotPropertiesInternal)GetProcAddress(vssapiBase, "VssFreeSnapshotPropertiesInternal");
        }

        PhEndInitOnce(&initOnce);
    }

    if (!CreateVssBackupComponentsInternal_I || !VssFreeSnapshotPropertiesInternal_I)
        return E_NOINTERFACE;

    result = CreateVssBackupComponentsInternal_I(&backupComponents);

    if (!SUCCEEDED(result))
        return result;

    vss = new BK_VSS_OBJECT;
    vss->Object = backupComponents;
    vss->SnapshotSetId = GUID_NULL;

    *Vss = vss;

    return result;
}

VOID BkDestroyVssObject(
    _In_ PBK_VSS_OBJECT Vss
    )
{
    std::vector<BK_VSS_SNAPSHOT>::iterator it;

    for (it = Vss->Snapshots.begin(); it != Vss->Snapshots.end(); ++it)
    {
        PhSwapReference((PVOID *)&it->Volume, NULL);
        PhSwapReference((PVOID *)&it->SnapshotDevice, NULL);
    }

    Vss->Object->Release();
    delete Vss;
}

HRESULT BkStartSnapshotsVssObject(
    _In_ PBK_VSS_OBJECT Vss
    )
{
    HRESULT result;

    result = Vss->Object->InitializeForBackup();

    if (!SUCCEEDED(result))
        return result;

    result = Vss->Object->SetBackupState(FALSE, FALSE, VSS_BT_INCREMENTAL);

    if (!SUCCEEDED(result))
        return result;

    result = Vss->Object->SetContext(VSS_CTX_FILE_SHARE_BACKUP);

    if (!SUCCEEDED(result))
        return result;

    return Vss->Object->StartSnapshotSet(&Vss->SnapshotSetId);
}

HRESULT BkAddSnapshotVssObject(
    _In_ PBK_VSS_OBJECT Vss,
    _In_ PPH_STRING Volume
    )
{
    HRESULT result;
    BOOLEAN volumeNeedsDeref;
    BK_VSS_SNAPSHOT snapshot;

    if (Volume->Length == 0)
        return E_INVALIDARG;

    volumeNeedsDeref = FALSE;

    if (Volume->Buffer[Volume->Length / sizeof(WCHAR) - 1] != '\\')
    {
        // AddToSnapshotSet needs a trailing backslash.
        Volume = PhConcatStrings2(Volume->Buffer, L"\\");
        volumeNeedsDeref = TRUE;
    }

    memset(&snapshot, 0, sizeof(BK_VSS_SNAPSHOT));
    snapshot.Volume = Volume;
    snapshot.VolumeSr = Volume->sr;
    snapshot.VolumeSr.Length -= sizeof(WCHAR); // remove trailing backslash

    result = Vss->Object->AddToSnapshotSet(Volume->Buffer, GUID_NULL, &snapshot.SnapshotId);

    if (SUCCEEDED(result))
    {
        PhReferenceObject(Volume);
        Vss->Snapshots.push_back(snapshot);
    }

    if (volumeNeedsDeref)
        PhDereferenceObject(Volume);

    return result;
}

bool BkpSnapshotSortFunction(
    _In_ BK_VSS_SNAPSHOT &Snapshot1,
    _In_ BK_VSS_SNAPSHOT &Snapshot2
    )
{
    SIZE_T length1;
    SIZE_T length2;

    length1 = Snapshot1.Volume ? Snapshot1.Volume->Length : 0;
    length2 = Snapshot2.Volume ? Snapshot2.Volume->Length : 0;

    return length2 < length1;
}

HRESULT BkpUpdateSnapshotProperties(
    _In_ PBK_VSS_OBJECT Vss
    )
{
    HRESULT result;
    std::vector<BK_VSS_SNAPSHOT>::iterator it;
    VSS_SNAPSHOT_PROP prop;

    for (it = Vss->Snapshots.begin(); it != Vss->Snapshots.end(); ++it)
    {
        if (SUCCEEDED(result = Vss->Object->GetSnapshotProperties(it->SnapshotId, &prop)))
        {
            PhMoveReference((PVOID *)&it->SnapshotDevice, PhCreateString(prop.m_pwszSnapshotDeviceObject));
            VssFreeSnapshotPropertiesInternal_I(&prop);
        }
        else
        {
            return result;
        }
    }

    std::sort(Vss->Snapshots.begin(), Vss->Snapshots.end(), BkpSnapshotSortFunction);

    return S_OK;
}

HRESULT BkPerformSnapshotsVssObject(
    _In_ PBK_VSS_OBJECT Vss
    )
{
    HRESULT result;
    IVssAsync *async;

    result = Vss->Object->DoSnapshotSet(&async);

    if (!SUCCEEDED(result))
        return result;

    result = async->Wait();

    if (SUCCEEDED(result))
    {
        if (SUCCEEDED(async->QueryStatus(&result, NULL)))
        {
            if (SUCCEEDED(result))
                result = S_OK;
        }
        else
        {
            return E_UNEXPECTED;
        }
    }

    async->Release();

    if (!SUCCEEDED(result))
        return result;

    result = BkpUpdateSnapshotProperties(Vss);

    return result;
}

PPH_STRING BkMapFileNameVssObject(
    _In_ PBK_VSS_OBJECT Vss,
    _In_ PPH_STRING FileName
    )
{
    std::vector<BK_VSS_SNAPSHOT>::iterator it;

    for (it = Vss->Snapshots.begin(); it != Vss->Snapshots.end(); ++it)
    {
        if (it->SnapshotDevice &&
            PhStartsWithStringRef(&FileName->sr, &it->VolumeSr, TRUE) &&
            (FileName->Length == it->VolumeSr.Length ||
            FileName->Buffer[it->VolumeSr.Length / sizeof(WCHAR)] == '\\'))
        {
            PPH_STRING newString;

            newString = PhCreateStringEx(NULL, FileName->Length - it->VolumeSr.Length + it->SnapshotDevice->Length);
            memcpy(newString->Buffer, it->SnapshotDevice->Buffer, it->SnapshotDevice->Length);
            memcpy((PCHAR)newString->Buffer + it->SnapshotDevice->Length, (PCHAR)FileName->Buffer + it->VolumeSr.Length, FileName->Length - it->VolumeSr.Length);

            return newString;
        }
    }

    PhReferenceObject(FileName);

    return FileName;
}
