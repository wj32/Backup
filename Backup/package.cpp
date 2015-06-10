/*
 * Backup -
 *   package management
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
#include "package.h"
#include "packagep.h"

static GUID IID_ISequentialInStream_I = { 0x23170f69, 0x40c1, 0x278a, { 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00 } };
static GUID IID_ISequentialOutStream_I = { 0x23170f69, 0x40c1, 0x278a, { 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00 } };
static GUID IID_IInStream_I = { 0x23170f69, 0x40c1, 0x278a, { 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00 } };
static GUID IID_IOutStream_I = { 0x23170f69, 0x40c1, 0x278a, { 0x00, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00 } };
static GUID IID_IStreamGetSize_I = { 0x23170f69, 0x40c1, 0x278a, { 0x00, 0x00, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00 } };
static GUID IID_IInArchive_I = { 0x23170f69, 0x40c1, 0x278a, { 0x00, 0x00, 0x00, 0x06, 0x00, 0x60, 0x00, 0x00 } };
static GUID IID_IOutArchive_I = { 0x23170f69, 0x40c1, 0x278a, { 0x00, 0x00, 0x00, 0x06, 0x00, 0xa0, 0x00, 0x00 } };
static GUID IID_IArchiveExtractCallback_I = { 0x23170f69, 0x40c1, 0x278a, { 0x00, 0x00, 0x00, 0x06, 0x00, 0x20, 0x00, 0x00 } };
static GUID IID_IArchiveUpdateCallback_I = { 0x23170f69, 0x40c1, 0x278a, { 0x00, 0x00, 0x00, 0x06, 0x00, 0x80, 0x00, 0x00 } };
static GUID SevenZipHandlerGuid = { 0x23170f69, 0x40c1, 0x278a, { 0x10, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00, 0x00 } };

static PH_INITONCE SevenZipInitOnce = PH_INITONCE_INIT;
static HMODULE SevenZipHandle;
static _CreateObject CreateObject_I;

HRESULT PkFileInStream::QueryInterface(REFIID Riid, void **ppvObject)
{
    return Parent->QueryInterface(Riid, ppvObject);
}

ULONG PkFileInStream::AddRef()
{
    return Parent->AddRef();
}

ULONG PkFileInStream::Release()
{
    return Parent->Release();
}

HRESULT PkFileInStream::Read(void *data, UInt32 size, UInt32 *processedSize)
{
    NTSTATUS status;

    if (Parent->Mode == PkPipeReaderFileStream)
    {
        PkFileStream *parentPipe;
        PCHAR currentData;
        ULONG remainingSize;
        ULONG availableSize;

        parentPipe = Parent->ParentPipe;
        currentData = (PCHAR)data;
        remainingSize = size;

        PhAcquireQueuedLockExclusive(&parentPipe->PipeLock);

        while (remainingSize != 0 && parentPipe->RemainingStreamSize != 0)
        {
            if (parentPipe->ReadPosition == parentPipe->ReadableSize)
            {
                if (parentPipe->RemainingStreamSize == 0 || parentPipe->WriteReferenceCount == 0)
                    break;

                PhPulseCondition(&parentPipe->PipeCondition);
                // Wait for the writer to fill up our buffer.
                PhWaitForCondition(&parentPipe->PipeCondition, &parentPipe->PipeLock, NULL);
                continue;
            }

            availableSize = (ULONG)(parentPipe->ReadableSize - parentPipe->ReadPosition);

            if (remainingSize >= availableSize)
            {
                memcpy(currentData, (PCHAR)parentPipe->Buffer + parentPipe->ReadPosition, availableSize);
                currentData += availableSize;
                parentPipe->ReadPosition += availableSize;

                if (parentPipe->RemainingStreamSize >= availableSize)
                    parentPipe->RemainingStreamSize -= availableSize;
                else
                    parentPipe->RemainingStreamSize = 0;

                remainingSize -= availableSize;
            }
            else
            {
                memcpy(currentData, (PCHAR)parentPipe->Buffer + parentPipe->ReadPosition, remainingSize);
                currentData += availableSize;
                parentPipe->ReadPosition += remainingSize;

                if (parentPipe->RemainingStreamSize >= remainingSize)
                    parentPipe->RemainingStreamSize -= remainingSize;
                else
                    parentPipe->RemainingStreamSize = 0;

                remainingSize = 0;
            }
        }

        PhReleaseQueuedLockExclusive(&parentPipe->PipeLock);

        *processedSize = size - remainingSize;

        return S_OK;
    }
    else if (Parent->Mode == PkPipeWriterFileStream)
    {
        return E_FAIL;
    }

    if (!Parent->FileStream)
    {
        *processedSize = 0;
        return S_OK;
    }

    *processedSize = 0;
    status = PhReadFileStream(Parent->FileStream, data, size, (PULONG)processedSize);

    if (status == STATUS_END_OF_FILE)
    {
        *processedSize = 0;
        return S_OK;
    }

    if (NT_SUCCESS(status))
        return S_OK;
    else
        return E_FAIL;
}

HRESULT PkFileInStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition)
{
    return Parent->Seek(offset, seekOrigin, newPosition);
}

HRESULT PkFileOutStream::QueryInterface(REFIID Riid, void **ppvObject)
{
    return Parent->QueryInterface(Riid, ppvObject);
}

ULONG PkFileOutStream::AddRef()
{
    return Parent->AddRef();
}

ULONG PkFileOutStream::Release()
{
    return Parent->Release();
}

HRESULT PkFileOutStream::Write(const void *data, UInt32 size, UInt32 *processedSize)
{
    NTSTATUS status;

    if (Parent->Mode == PkPipeWriterFileStream)
    {
        PkFileStream *parentPipe;
        PCHAR currentData;
        SIZE_T remainingSize;

        parentPipe = Parent->ParentPipe;
        currentData = (PCHAR)data;
        remainingSize = size;

        PhAcquireQueuedLockExclusive(&parentPipe->PipeLock);

        while (remainingSize != 0)
        {
            if (parentPipe->ReadPosition != parentPipe->ReadableSize)
            {
                if (parentPipe->ReadReferenceCount == 0)
                    break;

                // Wait for the reader to read everything.
                PhWaitForCondition(&parentPipe->PipeCondition, &parentPipe->PipeLock, NULL);
                continue;
            }

            if (remainingSize >= parentPipe->BufferSize)
            {
                memcpy(parentPipe->Buffer, currentData, parentPipe->BufferSize);
                currentData += parentPipe->BufferSize;
                parentPipe->ReadableSize = parentPipe->BufferSize;
                remainingSize -= parentPipe->BufferSize;
            }
            else
            {
                memcpy(parentPipe->Buffer, currentData, remainingSize);
                currentData += remainingSize;
                parentPipe->ReadableSize = remainingSize;
                remainingSize = 0;
            }

            parentPipe->ReadPosition = 0;
            PhPulseCondition(&parentPipe->PipeCondition);
        }

        PhReleaseQueuedLockExclusive(&parentPipe->PipeLock);

        *processedSize = size;

        return S_OK;
    }
    else if (Parent->Mode == PkPipeReaderFileStream)
    {
        return E_FAIL;
    }

    if (!Parent->FileStream)
    {
        *processedSize = size;
        return S_OK;
    }

    status = PhWriteFileStream(Parent->FileStream, (PVOID)data, size);
    *processedSize = size;

    if (NT_SUCCESS(status))
        return S_OK;
    else
        return E_FAIL;
}

HRESULT PkFileOutStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition)
{
    return Parent->Seek(offset, seekOrigin, newPosition);
}

HRESULT PkFileOutStream::SetSize(Int64 newSize)
{
    NTSTATUS status;
    LARGE_INTEGER size;

    if (Parent->Mode == PkPipeFileStream || Parent->Mode == PkPipeWriterFileStream || Parent->Mode == PkPipeReaderFileStream)
        return E_FAIL;
    if (!Parent->FileStream)
        return E_FAIL;

    size.QuadPart = newSize;
    PhFlushFileStream(Parent->FileStream, FALSE);
    status = PhSetFileSize(Parent->FileStream->FileHandle, &size);

    if (NT_SUCCESS(status))
        return S_OK;
    else
        return E_FAIL;
}

HRESULT PkFileStreamGetSize::QueryInterface(REFIID Riid, void **ppvObject)
{
    return Parent->QueryInterface(Riid, ppvObject);
}

ULONG PkFileStreamGetSize::AddRef()
{
    return Parent->AddRef();
}

ULONG PkFileStreamGetSize::Release()
{
    return Parent->Release();
}

HRESULT PkFileStreamGetSize::GetSize(UInt64 *size)
{
    LARGE_INTEGER fileSize;

    if (Parent->Mode == PkPipeFileStream)
    {
        *size = Parent->StreamSize;
        return S_OK;
    }
    else if (Parent->Mode == PkPipeWriterFileStream || Parent->Mode == PkPipeReaderFileStream)
    {
        *size = Parent->ParentPipe->StreamSize;
        return S_OK;
    }

    if (!Parent->FileStream)
    {
        *size = 0;
        return S_OK;
    }

    if (!NT_SUCCESS(PhGetFileSize(Parent->FileStream->FileHandle, &fileSize)))
        return E_FAIL;

    *size = fileSize.QuadPart;

    return S_OK;
}

HRESULT PkFileStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition)
{
    NTSTATUS status;
    LARGE_INTEGER offsetLi;
    PH_SEEK_ORIGIN origin;

    if (Mode == PkPipeFileStream || Mode == PkPipeWriterFileStream || Mode == PkPipeReaderFileStream)
        return E_FAIL;

    if (!FileStream)
        return S_OK;

    offsetLi.QuadPart = offset;

    switch (seekOrigin)
    {
    case STREAM_SEEK_SET:
        origin = SeekStart;
        break;
    case STREAM_SEEK_CUR:
        origin = SeekCurrent;
        break;
    case STREAM_SEEK_END:
        origin = SeekEnd;
        break;
    default:
        return E_INVALIDARG;
    }

    status = PhSeekFileStream(FileStream, &offsetLi, origin);

    if (!NT_SUCCESS(status))
        return E_FAIL;

    PhGetPositionFileStream(FileStream, &offsetLi);

    if (newPosition)
        *newPosition = offsetLi.QuadPart;

    return S_OK;
}

HRESULT PkArchiveUpdateCallback::QueryInterface(REFIID Riid, void **ppvObject)
{
    if (IsEqualIID(Riid, IID_IUnknown) ||
        IsEqualIID(Riid, IID_IArchiveUpdateCallback_I))
    {
        this->AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

ULONG PkArchiveUpdateCallback::AddRef()
{
    return ++this->ReferenceCount;
}

ULONG PkArchiveUpdateCallback::Release()
{
    if (--this->ReferenceCount == 0)
    {
        delete this;
    }

    return this->ReferenceCount;
}

HRESULT PkArchiveUpdateCallback::SetTotal(UInt64 total)
{
    ProgressTotal = total;
    return S_OK;
}

HRESULT PkArchiveUpdateCallback::SetCompleted(const UInt64 *completeValue)
{
    PK_PARAMETER_PROGRESS progress;

    if (completeValue)
        ProgressValue = *completeValue;

    progress.ProgressValue = ProgressValue;
    progress.ProgressTotal = ProgressTotal;
    Callback(PkProgressMessage, NULL, &progress, Context);

    return S_OK;
}

HRESULT PkArchiveUpdateCallback::GetUpdateItemInfo(UInt32 index, Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive)
{
    PPK_ACTION action;

    action = GetAction(index);

    if (!action)
        return E_ABORT;

    switch (action->Type)
    {
    case PkAddType:
        *newData = TRUE;
        *newProperties = TRUE;
        *indexInArchive = -1;
        break;
    case PkAddFromPackageType:
        *newData = TRUE;
        *newProperties = TRUE;
        *indexInArchive = -1;
        break;
    case PkUpdateType:
        *newData = FALSE;
        *newProperties = FALSE;
        *indexInArchive = action->u.Update.Index;
        break;
    }

    return S_OK;
}

HRESULT PkArchiveUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)
{
    PPK_ACTION action;
    FILE_NETWORK_OPEN_INFORMATION networkOpenInfo;

    action = GetAction(index);

    if (!action)
        return E_ABORT;

    if (action->Type == PkAddFromPackageType)
    {
        HRESULT result;
        IInArchive *package;

        package = (IInArchive *)action->u.AddFromPackage.Package;

        result = package->GetProperty(action->u.AddFromPackage.IndexInPackage, propID, value);

        if (!SUCCEEDED(result))
            return result;

        return S_OK;
    }
    else if (action->Type == PkUpdateType)
    {
        return InArchive->GetProperty(action->u.Update.Index, propID, value);
    }

    switch (propID)
    {
    case kpidPath:
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(action->u.Add.Destination->Buffer);
        break;
    case kpidIsDir:
        value->vt = VT_BOOL;
        value->boolVal = !!(action->u.Add.Flags & PK_ACTION_DIRECTORY);
        break;
    case kpidSize:
        networkOpenInfo.EndOfFile.QuadPart = 0;
        Callback(PkGetSizeMessage, action, &networkOpenInfo, Context);
        value->vt = VT_UI8;
        value->uhVal.QuadPart = networkOpenInfo.EndOfFile.QuadPart;
        break;
    case kpidAttrib:
        networkOpenInfo.FileAttributes = 0;
        Callback(PkGetAttributesMessage, action, &networkOpenInfo, Context);
        value->vt = VT_UI4;
        value->uintVal = networkOpenInfo.FileAttributes;
        break;
    case kpidCTime:
        networkOpenInfo.CreationTime.QuadPart = 0;
        Callback(PkGetCreationTimeMessage, action, &networkOpenInfo, Context);
        value->vt = VT_FILETIME;
        value->filetime.dwLowDateTime = networkOpenInfo.CreationTime.LowPart;
        value->filetime.dwHighDateTime = networkOpenInfo.CreationTime.HighPart;
        break;
    case kpidATime:
        networkOpenInfo.LastAccessTime.QuadPart = 0;
        Callback(PkGetAccessTimeMessage, action, &networkOpenInfo, Context);
        value->vt = VT_FILETIME;
        value->filetime.dwLowDateTime = networkOpenInfo.LastAccessTime.LowPart;
        value->filetime.dwHighDateTime = networkOpenInfo.LastAccessTime.HighPart;
        break;
    case kpidMTime:
        networkOpenInfo.LastWriteTime.QuadPart = 0;
        Callback(PkGetModifiedTimeMessage, action, &networkOpenInfo, Context);
        value->vt = VT_FILETIME;
        value->filetime.dwLowDateTime = networkOpenInfo.LastWriteTime.LowPart;
        value->filetime.dwHighDateTime = networkOpenInfo.LastWriteTime.HighPart;
        break;
    case kpidIsAnti:
        value->vt = VT_BOOL;
        value->boolVal = FALSE;
        break;
    }

    return S_OK;
}

HRESULT PkArchiveUpdateCallback::GetStream(UInt32 index, ISequentialInStream **inStream)
{
    HRESULT result;
    PPK_ACTION action;
    PK_PARAMETER_GET_STREAM getStream;
    PkFileStream *fileStream;

    action = GetAction(index);

    if (!action)
        return E_ABORT;

    if (action->Type == PkAddFromPackageType)
    {
        IInArchive *package;
        PROPVARIANT sizeValue;
        PkFileStream *pipe;
        IOutStream *writer;
        IInStream *reader;

        package = (IInArchive *)action->u.AddFromPackage.Package;

        PropVariantInit(&sizeValue);
        result = package->GetProperty(action->u.AddFromPackage.IndexInPackage, kpidSize, &sizeValue);

        if (!SUCCEEDED(result))
            return result;

        pipe = new PkFileStream(PkPipeFileStream, NULL, sizeValue.hVal.QuadPart);
        pipe->InitializePipe(&writer, &reader);
        pipe->Release();

        if (pipe->Buffer)
        {
            if (!ExtractCallback)
            {
                ExtractCallback = new PkUpdateArchiveExtractCallback();
                ExtractCallback->Owner = this;
                ExtractCallback->StartThread();
            }

            ExtractCallback->SetNewJob(package, action->u.AddFromPackage.IndexInPackage, writer);
            writer->Release();
            *inStream = reader;
        }
        else
        {
            writer->Release();
            reader->Release();

            return E_OUTOFMEMORY;
        }

        return S_OK;
    }
    else if (action->Type == PkUpdateType)
    {
        return E_INVALIDARG;
    }

    memset(&getStream, 0, sizeof(PK_PARAMETER_GET_STREAM));

    if (!SUCCEEDED(result = Callback(PkGetStreamMessage, action, &getStream, Context)))
    {
        *inStream = NULL;
        return result;
    }

    if (!getStream.FileStream)
    {
        *inStream = NULL;
        return E_FAIL;
    }

    fileStream = (PkFileStream *)getStream.FileStream;
    *inStream = &fileStream->InStream;

    return S_OK;
}

HRESULT PkArchiveUpdateCallback::SetOperationResult(Int32 operationResult)
{
    return S_OK;
}

VOID PkArchiveUpdateCallback::CreateActionMap()
{
    PPK_ACTION_SEGMENT segment;
    ULONG i;
    ULONG index;

    segment = ActionList->FirstSegment;
    index = 0;

    while (segment)
    {
        for (i = 0; i < segment->Count; i++)
        {
            ActionMap[index] = &segment->Actions[i];
            index++;
        }

        segment = segment->Next;
    }
}

PPK_ACTION PkArchiveUpdateCallback::GetAction(ULONG index)
{
    std::unordered_map<ULONG, PPK_ACTION>::iterator it;

    it = ActionMap.find(index);

    if (it == ActionMap.end())
        return NULL;

    return it->second;
}

HRESULT PkUpdateArchiveExtractCallback::QueryInterface(REFIID Riid, void **ppvObject)
{
    if (IsEqualIID(Riid, IID_IUnknown) ||
        IsEqualIID(Riid, IID_IArchiveExtractCallback_I))
    {
        this->AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

ULONG PkUpdateArchiveExtractCallback::AddRef()
{
    return ++this->ReferenceCount;
}

ULONG PkUpdateArchiveExtractCallback::Release()
{
    if (--this->ReferenceCount == 0)
    {
        delete this;
    }

    return this->ReferenceCount;
}

HRESULT PkUpdateArchiveExtractCallback::SetTotal(UInt64 total)
{
    return S_OK;
}

HRESULT PkUpdateArchiveExtractCallback::SetCompleted(const UInt64 *completeValue)
{
    return S_OK;
}

HRESULT PkUpdateArchiveExtractCallback::GetStream(UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode)
{
    IInArchive *currentArchive;

    // This code executes with the lock held.

    currentArchive = InArchive;

    // Wait until we get a job or the archive has changed.
    while (!ThreadStopping && InArchive == currentArchive && ItemIndex == -1)
    {
        PhWaitForCondition(&Condition, &Lock, NULL);
    }

    if (InArchive != currentArchive)
        return E_ABORT;

    if (index < ItemIndex)
    {
        // Keep searching.
        *outStream = &(new PkFileStream(PkZeroFileStream, NULL))->OutStream;
        return S_OK;
    }
    else if (index == ItemIndex)
    {
        *outStream = OutStream;
        OutStream = NULL;
        ItemIndex = -1;
        PhPulseCondition(&Condition);

        return S_OK;
    }
    else
    {
        // We've gone past the item.
        return E_ABORT;
    }
}

HRESULT PkUpdateArchiveExtractCallback::PrepareOperation(Int32 askExtractMode)
{
    return S_OK;
}

HRESULT PkUpdateArchiveExtractCallback::SetOperationResult(Int32 resultEOperationResult)
{
    if (!SUCCEEDED(resultEOperationResult))
        Result = resultEOperationResult;

    return S_OK;
}

VOID PkUpdateArchiveExtractCallback::StartThread()
{
    ThreadStarted = TRUE;
    PhQueueItemGlobalWorkQueue(ThreadStart, this);
}

VOID PkUpdateArchiveExtractCallback::StopThread()
{
    SetNewJob(NULL, -1, NULL);

    PhAcquireQueuedLockExclusive(&Lock);
    ThreadStopping = TRUE;
    PhPulseCondition(&Condition);
    PhReleaseQueuedLockExclusive(&Lock);
}

BOOLEAN PkUpdateArchiveExtractCallback::WaitForThread()
{
    if (NtWaitForSingleObject(ThreadFinishEvent, FALSE, NULL) == STATUS_WAIT_0)
        return TRUE;
    else
        return FALSE;
}

VOID PkUpdateArchiveExtractCallback::SetNewJob(IInArchive *NewInArchive, ULONG NewItemIndex, IOutStream *NewOutStream)
{
    PhAcquireQueuedLockExclusive(&Lock);

    // Wait for the current job to finish.
    while (!ThreadStopping && ItemIndex != -1)
        PhWaitForCondition(&Condition, &Lock, NULL);

    if (ThreadStopping)
        return;

    if (NewInArchive)
        NewInArchive->AddRef();
    if (InArchive)
        InArchive->Release();

    InArchive = NewInArchive;

    ItemIndex = NewItemIndex;

    if (NewOutStream)
        NewOutStream->AddRef();
    if (OutStream)
        OutStream->Release();

    OutStream = NewOutStream;

    PhPulseCondition(&Condition);

    PhReleaseQueuedLockExclusive(&Lock);
}

VOID PkUpdateArchiveExtractCallback::WaitForJob()
{
    PhAcquireQueuedLockExclusive(&Lock);

    while (InArchive && ItemIndex != -1)
        PhWaitForCondition(&Condition, &Lock, NULL);

    PhReleaseQueuedLockExclusive(&Lock);
}

VOID PkUpdateArchiveExtractCallback::Run()
{
    HRESULT result;
    IInArchive *currentArchive;

    PhAcquireQueuedLockExclusive(&Lock);

    while (!ThreadStopping)
    {
        if (!InArchive)
        {
            PhWaitForCondition(&Condition, &Lock, NULL);
            continue;
        }

        currentArchive = InArchive;

        currentArchive->AddRef();
        result = currentArchive->Extract(NULL, -1, FALSE, this);
        currentArchive->Release();

        if (result != E_ABORT && !SUCCEEDED(result))
            Result = result;
    }

    PhReleaseQueuedLockExclusive(&Lock);
}

HRESULT PkArchiveExtractCallback::QueryInterface(REFIID Riid, void **ppvObject)
{
    if (IsEqualIID(Riid, IID_IUnknown) ||
        IsEqualIID(Riid, IID_IArchiveExtractCallback_I))
    {
        this->AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

ULONG PkArchiveExtractCallback::AddRef()
{
    return ++this->ReferenceCount;
}

ULONG PkArchiveExtractCallback::Release()
{
    if (--this->ReferenceCount == 0)
    {
        delete this;
    }

    return this->ReferenceCount;
}

HRESULT PkArchiveExtractCallback::SetTotal(UInt64 total)
{
    ProgressTotal = total;
    return S_OK;
}

HRESULT PkArchiveExtractCallback::SetCompleted(const UInt64 *completeValue)
{
    PK_PARAMETER_PROGRESS progress;

    if (completeValue)
        ProgressValue = *completeValue;

    progress.ProgressValue = ProgressValue;
    progress.ProgressTotal = ProgressTotal;
    Callback(PkProgressMessage, NULL, &progress, Context);

    return S_OK;
}

HRESULT PkArchiveExtractCallback::GetStream(UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode)
{
    HRESULT result;
    PPK_ACTION action;
    PK_ACTION localAction;
    PK_PARAMETER_GET_STREAM getStream;
    PROPVARIANT value;

    if (ActionList)
    {
        action = GetAction(index);

        if (!action)
        {
            // We don't want this file.
            *outStream = &(new PkFileStream(PkZeroFileStream, NULL))->OutStream;
            return S_OK;
        }
    }
    else
    {
        localAction.Type = PkUpdateType;
        localAction.Context = NULL;
        localAction.u.Update.Index = index;
        action = &localAction;
    }

    memset(&getStream, 0, sizeof(PK_PARAMETER_GET_STREAM));

    PropVariantInit(&value);
    InArchive->GetProperty(index, kpidAttrib, &value);
    getStream.FileInformation.FileAttributes = value.uintVal;

    PropVariantClear(&value);
    InArchive->GetProperty(index, kpidCTime, &value);

    if (value.vt != VT_EMPTY)
    {
        getStream.FileInformation.CreationTime.LowPart = value.filetime.dwLowDateTime;
        getStream.FileInformation.CreationTime.HighPart = value.filetime.dwHighDateTime;
    }

    PropVariantClear(&value);
    InArchive->GetProperty(index, kpidATime, &value);

    if (value.vt != VT_EMPTY)
    {
        getStream.FileInformation.LastAccessTime.LowPart = value.filetime.dwLowDateTime;
        getStream.FileInformation.LastAccessTime.HighPart = value.filetime.dwHighDateTime;
    }

    PropVariantClear(&value);
    InArchive->GetProperty(index, kpidMTime, &value);

    if (value.vt != VT_EMPTY)
    {
        getStream.FileInformation.LastWriteTime.LowPart = value.filetime.dwLowDateTime;
        getStream.FileInformation.LastWriteTime.HighPart = value.filetime.dwHighDateTime;
        getStream.FileInformation.ChangeTime = getStream.FileInformation.LastWriteTime; // fake value
    }

    PropVariantClear(&value);

    result = Callback(PkGetStreamMessage, action, &getStream, Context);

    if (!SUCCEEDED(result))
        return result;

    if (getStream.FileStream)
        *outStream = &((PkFileStream *)getStream.FileStream)->OutStream;
    else
        *outStream = &(new PkFileStream(PkZeroFileStream, NULL))->OutStream;

    return S_OK;
}

HRESULT PkArchiveExtractCallback::PrepareOperation(Int32 askExtractMode)
{
    return S_OK;
}

HRESULT PkArchiveExtractCallback::SetOperationResult(Int32 resultEOperationResult)
{
    return S_OK;
}

VOID PkArchiveExtractCallback::CreateActionMap()
{
    PPK_ACTION_SEGMENT segment;
    ULONG i;

    segment = ActionList->FirstSegment;

    while (segment)
    {
        for (i = 0; i < segment->Count; i++)
        {
            ActionMap[segment->Actions[i].u.Update.Index] = &segment->Actions[i];
        }

        segment = segment->Next;
    }
}

PPK_ACTION PkArchiveExtractCallback::GetAction(ULONG index)
{
    std::unordered_map<ULONG, PPK_ACTION>::iterator it;

    it = ActionMap.find(index);

    if (it == ActionMap.end())
        return NULL;

    return it->second;
}

PPK_FILE_STREAM PkCreateFileStream(
    _In_opt_ PPH_FILE_STREAM FileStream
    )
{
    PkFileStream *fileStream;

    fileStream = new PkFileStream(FileStream ? PkNormalFileStream : PkZeroFileStream, FileStream);

    return fileStream;
}

VOID PkReferenceFileStream(
    _In_ PPK_FILE_STREAM FileStream
    )
{
    PkFileStream *fileStream;

    fileStream = (PkFileStream *)FileStream;
    fileStream->AddRef();
}

VOID PkDereferenceFileStream(
    _In_ PPK_FILE_STREAM FileStream
    )
{
    PkFileStream *fileStream;

    fileStream = (PkFileStream *)FileStream;
    fileStream->Release();
}

PPK_ACTION_LIST PkCreateActionList(
    VOID
    )
{
    PPK_ACTION_LIST list;

    list = (PPK_ACTION_LIST)PhAllocate(sizeof(PK_ACTION_LIST));
    list->FirstSegment = NULL;
    list->LastSegment = NULL;
    list->NumberOfActions = 0;

    return list;
}

VOID PkDestroyActionList(
    _In_ PPK_ACTION_LIST List
    )
{
    PPK_ACTION_SEGMENT segment;
    PPK_ACTION_SEGMENT nextSegment;
    ULONG i;

    segment = List->FirstSegment;

    while (segment)
    {
        for (i = 0; i < segment->Count; i++)
        {
            PkpDeleteAction(&segment->Actions[i]);
        }

        nextSegment = segment->Next;
        PhFree(segment);
        segment = nextSegment;
    }

    PhFree(List);
}

ULONG PkQueryCountActionList(
    _In_ PPK_ACTION_LIST List
    )
{
    return List->NumberOfActions;
}

VOID PkAppendAddToActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ ULONG Flags,
    _In_ PPH_STRING Destination,
    _In_opt_ PVOID Context
    )
{
    PK_ACTION action;

    PhReferenceObject(Destination);
    action.Type = PkAddType;
    action.u.Add.Flags = Flags;
    action.u.Add.Destination = Destination;
    action.Context = Context;
    PkpAddToActionList(List, &action);
}

VOID PkAppendAddFromPackageToActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ PPK_PACKAGE Package,
    _In_ ULONG IndexInPackage,
    _In_opt_ PVOID Context
    )
{
    PK_ACTION action;

    ((IInArchive *)Package)->AddRef();
    action.Type = PkAddFromPackageType;
    action.u.AddFromPackage.Package = Package;
    action.u.AddFromPackage.IndexInPackage = IndexInPackage;
    action.Context = Context;
    PkpAddToActionList(List, &action);
}

VOID PkAppendUpdateToActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ ULONG Index,
    _In_opt_ PVOID Context
    )
{
    PK_ACTION action;

    action.Type = PkUpdateType;
    action.u.Update.Index = Index;
    action.Context = Context;
    PkpAddToActionList(List, &action);
}

PPK_ACTION PkIndexInActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ ULONG Index,
    _Out_opt_ PPK_ACTION_SEGMENT *Segment
    )
{
    ULONG segmentIndex;
    ULONG indexInSegment;
    PPK_ACTION_SEGMENT segment;

    segmentIndex = Index >> PK_ACTION_SEGMENT_SHIFT;
    indexInSegment = Index & (PK_ACTION_SEGMENT_SIZE - 1);
    segment = List->FirstSegment;

    while (segment)
    {
        if (segment->SegmentIndex == segmentIndex)
        {
            if (indexInSegment >= segment->Count)
                return NULL;

            if (Segment)
                *Segment = segment;

            return &segment->Actions[indexInSegment];
        }

        segment = segment->Next;
    }

    return NULL;
}

PPK_ACTION PkGetNextAction(
    _In_ PPK_ACTION Action,
    _In_ ULONG Index,
    _In_ PPK_ACTION_SEGMENT Segment,
    _Out_opt_ PPK_ACTION_SEGMENT *NewSegment
    )
{
    if (Index == PK_ACTION_SEGMENT_SIZE)
    {
        if (Segment->Next && Segment->Next->Count != 0)
        {
            if (NewSegment)
                *NewSegment = Segment->Next;

            return &Segment->Next->Actions[0];
        }

        return NULL;
    }

    if (Index + 1 < Segment->Count)
    {
        if (NewSegment)
            *NewSegment = Segment;

        return &Segment->Actions[Index + 1];
    }

    return NULL;
}

VOID PkpDeleteAction(
    _In_ PPK_ACTION Action
    )
{
    switch (Action->Type)
    {
    case PkAddType:
        PhDereferenceObject(Action->u.Add.Destination);
        break;
    case PkAddFromPackageType:
        ((IInArchive *)Action->u.AddFromPackage.Package)->Release();
        break;
    }
}

PPK_ACTION_SEGMENT PkpAllocateActionSegment(
    _In_ ULONG SegmentIndex
    )
{
    PPK_ACTION_SEGMENT segment;

    segment = (PPK_ACTION_SEGMENT)PhAllocate(sizeof(PK_ACTION_SEGMENT));
    segment->Next = NULL;
    segment->SegmentIndex = SegmentIndex;
    segment->Count = 0;

    return segment;
}

VOID PkpAddToActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ PPK_ACTION Action
    )
{
    PPK_ACTION_SEGMENT segment;

    if (!List->LastSegment)
    {
        List->LastSegment = PkpAllocateActionSegment(0);
        List->FirstSegment = List->LastSegment;
        List->NumberOfSegments = 1;
    }

    segment = List->LastSegment;

    if (segment->Count == PK_ACTION_SEGMENT_SIZE)
    {
        segment = PkpAllocateActionSegment(List->NumberOfSegments);
        List->NumberOfSegments++;
        List->LastSegment->Next = segment;
        List->LastSegment = segment;
    }

    segment->Actions[segment->Count] = *Action;
    segment->Count++;
    List->NumberOfActions++;
}

HRESULT PkpCreateSevenZipObject(
    _In_ PGUID ClassId,
    _In_ PGUID InterfaceId,
    _Out_ PVOID *Object
    )
{
    if (PhBeginInitOnce(&SevenZipInitOnce))
    {
        SevenZipHandle = LoadLibrary(L"7z.dll");

        if (SevenZipHandle)
        {
            CreateObject_I = (_CreateObject)GetProcAddress(SevenZipHandle, "CreateObject");
        }

        PhEndInitOnce(&SevenZipInitOnce);
    }

    if (!CreateObject_I)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    return CreateObject_I(ClassId, InterfaceId, Object);
}

HRESULT PkpCloseExtractCallback(
    _In_ PkArchiveUpdateCallback *UpdateCallback
    )
{
    HRESULT result;

    result = S_OK;

    if (UpdateCallback->ExtractCallback)
    {
        UpdateCallback->ExtractCallback->StopThread();
        UpdateCallback->ExtractCallback->WaitForThread();
        result = UpdateCallback->ExtractCallback->Result;
        UpdateCallback->ExtractCallback->Release();
        UpdateCallback->ExtractCallback = NULL;
    }

    return result;
}

HRESULT PkCreatePackage(
    _In_ PPK_FILE_STREAM FileStream,
    _In_ PPK_ACTION_LIST ActionList,
    _In_ PPK_PACKAGE_CALLBACK Callback,
    _In_opt_ PVOID Context
    )
{
    HRESULT result;
    PkFileStream *fileStream;
    IOutArchive *outArchive;
    PkArchiveUpdateCallback *updateCallback;

    if (ActionList->NumberOfActions == 0)
        return S_OK;

    fileStream = (PkFileStream *)FileStream;
    result = PkpCreateSevenZipObject(&SevenZipHandlerGuid, &IID_IOutArchive_I, (void **)&outArchive);

    if (!SUCCEEDED(result))
        return result;

    updateCallback = new PkArchiveUpdateCallback;
    updateCallback->ReferenceCount = 1;
    updateCallback->ActionList = ActionList;
    updateCallback->Callback = Callback;
    updateCallback->Context = Context;
    updateCallback->InArchive = NULL;
    updateCallback->CreateActionMap();

    result = outArchive->UpdateItems(&fileStream->OutStream, ActionList->NumberOfActions, updateCallback);

    if (SUCCEEDED(result))
        result = PkpCloseExtractCallback(updateCallback);
    else
        PkpCloseExtractCallback(updateCallback);

    updateCallback->Release();
    outArchive->Release();

    return result;
}

VOID PkReferencePackage(
    _In_ PPK_PACKAGE Package
    )
{
    ((IInArchive *)Package)->AddRef();
}

VOID PkDereferencePackage(
    _In_ PPK_PACKAGE Package
    )
{
    ((IInArchive *)Package)->Release();
}

HRESULT PkOpenPackageWithFilter(
    _In_ PPK_FILE_STREAM FileStream,
    _Inout_opt_ PPK_ACTION_LIST ActionList,
    _In_opt_ PPK_PACKAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ PPK_PACKAGE *Package
    )
{
    HRESULT result;
    PkFileStream *fileStream;
    IInArchive *inArchive;
    ULONG numberOfItems;
    ULONG i;

    fileStream = (PkFileStream *)FileStream;
    result = PkpCreateSevenZipObject(&SevenZipHandlerGuid, &IID_IInArchive_I, (void **)&inArchive);

    if (!SUCCEEDED(result))
        return result;

    result = inArchive->Open(&fileStream->InStream, NULL, NULL);

    if (!SUCCEEDED(result))
    {
        inArchive->Release();
        return result;
    }

    if (ActionList)
    {
        result = inArchive->GetNumberOfItems((UInt32 *)&numberOfItems);

        if (SUCCEEDED(result))
        {
            if (Callback)
            {
                for (i = 0; i < numberOfItems; i++)
                {
                    PK_PARAMETER_FILTER_ITEM filterItem;
                    PROPVARIANT pathValue;
                    PROPVARIANT attributesValue;
                    PROPVARIANT isDirValue;

                    PropVariantInit(&pathValue);
                    PropVariantInit(&attributesValue);
                    PropVariantInit(&isDirValue);

                    if (SUCCEEDED(inArchive->GetProperty(i, kpidPath, &pathValue)) &&
                        pathValue.vt == VT_BSTR &&
                        SUCCEEDED(inArchive->GetProperty(i, kpidAttrib, &attributesValue)) &&
                        SUCCEEDED(inArchive->GetProperty(i, kpidIsDir, &isDirValue)))
                    {
                        filterItem.Reject = FALSE;
                        filterItem.Path.Length = wcslen(pathValue.bstrVal) * sizeof(WCHAR);
                        filterItem.Path.Buffer = pathValue.bstrVal;
                        filterItem.FileAttributes = attributesValue.uintVal;
                        filterItem.NewContext = NULL;

                        if (isDirValue.boolVal)
                            filterItem.FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

                        Callback(PkFilterItemMessage, NULL, &filterItem, Context);

                        if (!filterItem.Reject)
                        {
                            PkAppendUpdateToActionList(ActionList, i, filterItem.NewContext);
                        }

                        PropVariantClear(&pathValue);
                        PropVariantClear(&attributesValue);
                        PropVariantClear(&isDirValue);
                    }
                    else
                    {
                        result = E_FAIL;
                        PropVariantClear(&pathValue);
                        PropVariantClear(&attributesValue);
                        PropVariantClear(&isDirValue);
                        break;
                    }
                }
            }
            else
            {
                for (i = 0; i < numberOfItems; i++)
                {
                    PkAppendUpdateToActionList(ActionList, i, NULL);
                }
            }
        }
    }

    if (SUCCEEDED(result))
        *Package = (PPK_PACKAGE)inArchive;
    else
        inArchive->Release();

    return result;
}

HRESULT PkUpdatePackage(
    _In_ PPK_FILE_STREAM FileStream,
    _In_ PPK_PACKAGE Package,
    _In_ PPK_ACTION_LIST ActionList,
    _In_ PPK_PACKAGE_CALLBACK Callback,
    _In_opt_ PVOID Context
    )
{
    HRESULT result;
    IInArchive *inArchive;
    PkFileStream *fileStream;
    IOutArchive *outArchive;
    PkArchiveUpdateCallback *updateCallback;

    inArchive = (IInArchive *)Package;
    fileStream = (PkFileStream *)FileStream;
    result = inArchive->QueryInterface(IID_IOutArchive_I, (void **)&outArchive);

    if (!SUCCEEDED(result))
        return result;

    updateCallback = new PkArchiveUpdateCallback;
    updateCallback->ReferenceCount = 1;
    updateCallback->ActionList = ActionList;
    updateCallback->Callback = Callback;
    updateCallback->Context = Context;
    updateCallback->InArchive = inArchive;
    updateCallback->CreateActionMap();

    result = outArchive->UpdateItems(&fileStream->OutStream, ActionList->NumberOfActions, updateCallback);

    if (SUCCEEDED(result))
        result = PkpCloseExtractCallback(updateCallback);
    else
        PkpCloseExtractCallback(updateCallback);

    updateCallback->Release();
    outArchive->Release();

    return result;
}

HRESULT PkExtractPackage(
    _In_ PPK_PACKAGE Package,
    _In_opt_ PPK_ACTION_LIST ActionList,
    _In_ PPK_PACKAGE_CALLBACK Callback,
    _In_opt_ PVOID Context
    )
{
    HRESULT result;
    IInArchive *inArchive;
    PkArchiveExtractCallback *extractCallback;
    PULONG items;
    ULONG numberOfItems;
    PPK_ACTION_SEGMENT segment;
    ULONG i;

    result = S_OK;
    inArchive = (IInArchive *)Package;

    extractCallback = new PkArchiveExtractCallback;
    extractCallback->ReferenceCount = 1;
    extractCallback->ActionList = ActionList;
    extractCallback->Callback = Callback;
    extractCallback->Context = Context;
    extractCallback->InArchive = inArchive;

    if (ActionList)
    {
        items = (PULONG)PhAllocate(sizeof(ULONG) * ActionList->NumberOfActions);
        segment = ActionList->FirstSegment;
        numberOfItems = 0;

        while (segment)
        {
            for (i = 0; i < segment->Count; i++)
            {
                if (numberOfItems >= ActionList->NumberOfActions)
                {
                    result = E_INVALIDARG;
                    break;
                }

                if (segment->Actions[i].Type == PkUpdateType)
                {
                    items[numberOfItems] = segment->Actions[i].u.Update.Index;
                    numberOfItems++;
                }
            }

            segment = segment->Next;
        }

        extractCallback->CreateActionMap();
    }
    else
    {
        items = NULL;
        numberOfItems = -1;
    }

    if (!SUCCEEDED(result))
        return result;

    result = inArchive->Extract((UInt32 *)items, numberOfItems, FALSE, extractCallback);

    if (items)
        PhFree(items);

    extractCallback->Release();

    return result;
}
