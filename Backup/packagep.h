#ifndef PACKAGEP_H
#define PACKAGEP_H

#include <unordered_map>
#include "lzma/CPP/7zip/Archive/IArchive.h"
#include "lzma/CPP/7zip/ICoder.h"

// File stream

typedef HRESULT (STDAPICALLTYPE *_CreateObject)(const GUID *clsid, const GUID *iid, void **outObject);

class PkFileStream;
class PkUpdateArchiveExtractCallback;

extern GUID IID_ISequentialInStream_I;
extern GUID IID_ISequentialOutStream_I;
extern GUID IID_IInStream_I;
extern GUID IID_IOutStream_I;
extern GUID IID_IStreamGetSize_I;

class PkFileInStream : public IInStream
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppvObject);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    HRESULT STDMETHODCALLTYPE Read(void *data, UInt32 size, UInt32 *processedSize);
    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition);

public:
    PkFileStream *Parent;
};

class PkFileOutStream : public IOutStream
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppvObject);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    HRESULT STDMETHODCALLTYPE Write(const void *data, UInt32 size, UInt32 *processedSize);
    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition);
    HRESULT STDMETHODCALLTYPE SetSize(Int64 newSize);

public:
    PkFileStream *Parent;
};

class PkFileStreamGetSize : public IStreamGetSize
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppvObject);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    HRESULT STDMETHODCALLTYPE GetSize(UInt64 *size);

public:
    PkFileStream *Parent;
};

enum PkFileStreamMode
{
    PkZeroFileStream,
    PkNormalFileStream,
    PkPipeFileStream,
    PkPipeWriterFileStream,
    PkPipeReaderFileStream
};

class PkFileStream
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppvObject)
    {
        if (IsEqualIID(Riid, IID_IUnknown) ||
            IsEqualIID(Riid, IID_ISequentialInStream_I) ||
            IsEqualIID(Riid, IID_IInStream_I))
        {
            AddRef();
            *ppvObject = &InStream;
            return S_OK;
        }
        else if (IsEqualIID(Riid, IID_ISequentialOutStream_I) ||
            IsEqualIID(Riid, IID_IOutStream_I))
        {
            AddRef();
            *ppvObject = &OutStream;
            return S_OK;
        }
        else if (IsEqualIID(Riid, IID_IStreamGetSize_I))
        {
            AddRef();
            *ppvObject = &StreamGetSize;
            return S_OK;
        }

        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        return ++ReferenceCount;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        if (--ReferenceCount == 0)
        {
            delete this;
        }

        return ReferenceCount;
    }

    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition);

public:
    PkFileStreamMode Mode;
    PPH_FILE_STREAM FileStream;
    PkFileInStream InStream;
    PkFileOutStream OutStream;
    PkFileStreamGetSize StreamGetSize;

    // Pipe
    PkFileStream *ParentPipe;
    ULONGLONG StreamSize;
    ULONGLONG RemainingStreamSize;
    PVOID Buffer;
    SIZE_T BufferSize;
    PH_QUEUED_LOCK PipeLock;
    PH_QUEUED_LOCK PipeCondition;
    SIZE_T ReadPosition;
    SIZE_T ReadableSize;
    ULONG WriteReferenceCount;
    ULONG ReadReferenceCount;

    PkFileStream(PkFileStreamMode mode, PPH_FILE_STREAM fileStream, ULONGLONG streamSize = 0)
        : ReferenceCount(1), FileStream(fileStream), ParentPipe(NULL), StreamSize(streamSize), Buffer(NULL), WriteReferenceCount(0), ReadReferenceCount(0)
    {
        if (FileStream)
            PhReferenceObject(FileStream);
        else
            mode = PkZeroFileStream;

        Mode = mode;
        InStream.Parent = this;
        OutStream.Parent = this;
        StreamGetSize.Parent = this;

        PhInitializeQueuedLock(&PipeLock);
        PhInitializeQueuedLock(&PipeCondition);
    }

    ~PkFileStream()
    {
        assert(WriteReferenceCount == 0 && ReadReferenceCount == 0);

        if (Mode == PkPipeWriterFileStream)
        {
            PhAcquireQueuedLockExclusive(&ParentPipe->PipeLock);

            if (--ParentPipe->WriteReferenceCount == 0)
            {
                PhPulseAllCondition(&ParentPipe->PipeCondition);
            }

            PhReleaseQueuedLockExclusive(&ParentPipe->PipeLock);
        }
        else if (Mode == PkPipeReaderFileStream)
        {
            PhAcquireQueuedLockExclusive(&ParentPipe->PipeLock);

            if (--ParentPipe->ReadReferenceCount == 0)
            {
                PhPulseAllCondition(&ParentPipe->PipeCondition);
            }

            PhReleaseQueuedLockExclusive(&ParentPipe->PipeLock);
        }

        if (FileStream)
            PhDereferenceObject(FileStream);
        if (Buffer)
            PhFreePage(Buffer);
    }

    VOID InitializePipe(IOutStream **Writer, IInStream **Reader)
    {
        PkFileStream *writer;
        PkFileStream *reader;

        Mode = PkPipeFileStream;

        RemainingStreamSize = StreamSize;
        Buffer = PhAllocatePage(PAGE_SIZE * 4, &BufferSize);
        ReadPosition = 0;
        ReadableSize = 0;

        writer = new PkFileStream(PkPipeWriterFileStream, NULL);
        writer->InitializePipeWriter(this);
        reader = new PkFileStream(PkPipeReaderFileStream, NULL);
        reader->InitializePipeReader(this);

        *Writer = &writer->OutStream;
        *Reader = &reader->InStream;
    }

    VOID InitializePipeWriter(PkFileStream *Parent)
    {
        Mode = PkPipeWriterFileStream;
        ParentPipe = Parent;
        ParentPipe->WriteReferenceCount++;
        ParentPipe->AddRef();
    }

    VOID InitializePipeReader(PkFileStream *Parent)
    {
        Mode = PkPipeReaderFileStream;
        ParentPipe = Parent;
        ParentPipe->ReadReferenceCount++;
        ParentPipe->AddRef();
    }

private:
    ULONG ReferenceCount;
};

class PkArchiveUpdateCallback : public IArchiveUpdateCallback
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppvObject);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    INTERFACE_IArchiveUpdateCallback(;)

public:
    ULONG ReferenceCount;
    PPK_ACTION_LIST ActionList;
    PPK_PACKAGE_CALLBACK Callback;
    PVOID Context;
    ULONGLONG ProgressValue;
    ULONGLONG ProgressTotal;
    IInArchive *InArchive;
    std::unordered_map<ULONG, PPK_ACTION> ActionMap;

    PkUpdateArchiveExtractCallback *ExtractCallback;

    PkArchiveUpdateCallback()
        : ExtractCallback(NULL)
    { }

    VOID CreateActionMap();

private:
    PPK_ACTION GetAction(ULONG index);
};

class PkUpdateArchiveExtractCallback : public IArchiveExtractCallback
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppvObject);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    INTERFACE_IArchiveExtractCallback(;)

public:
    ULONG ReferenceCount;
    PkArchiveUpdateCallback *Owner;
    IInArchive *InArchive;
    ULONG ItemIndex;
    IOutStream *OutStream;
    HRESULT Result;
    BOOLEAN ThreadStarted;
    BOOLEAN ThreadStopping;
    HANDLE ThreadFinishEvent;
    PH_QUEUED_LOCK Lock;
    PH_QUEUED_LOCK Condition;

    VOID StartThread();
    VOID StopThread();
    BOOLEAN WaitForThread();
    VOID SetNewJob(IInArchive *NewInArchive, ULONG NewItemIndex, IOutStream *NewOutStream);
    VOID WaitForJob();
    VOID Run();

    static NTSTATUS NTAPI ThreadStart(PVOID Parameter)
    {
        PkUpdateArchiveExtractCallback *extractCallback = (PkUpdateArchiveExtractCallback *)Parameter;

        extractCallback->Run();

        if (extractCallback->ThreadFinishEvent)
            NtSetEvent(extractCallback->ThreadFinishEvent, NULL);

        return STATUS_SUCCESS;
    }

    PkUpdateArchiveExtractCallback()
    {
        ReferenceCount = 1;
        InArchive = NULL;
        ItemIndex = -1;
        OutStream = NULL;
        Result = S_OK;
        ThreadStarted = FALSE;
        ThreadStopping = FALSE;
        PhInitializeQueuedLock(&Lock);
        PhInitializeQueuedLock(&Condition);
        ThreadFinishEvent = NULL;
        NtCreateEvent(&ThreadFinishEvent, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
    }

    ~PkUpdateArchiveExtractCallback()
    {
        if (InArchive)
            InArchive->Release();
        if (OutStream)
            OutStream->Release();
        if (ThreadFinishEvent)
            NtClose(ThreadFinishEvent);
    }
};

class PkArchiveExtractCallback : public IArchiveExtractCallback
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppvObject);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    INTERFACE_IArchiveExtractCallback(;)

public:
    ULONG ReferenceCount;
    PPK_ACTION_LIST ActionList;
    PPK_PACKAGE_CALLBACK Callback;
    PVOID Context;
    ULONGLONG ProgressValue;
    ULONGLONG ProgressTotal;
    IInArchive *InArchive;
    std::unordered_map<ULONG, PPK_ACTION> ActionMap;

    VOID CreateActionMap();

private:
    PPK_ACTION GetAction(ULONG index);
};

// Action list

VOID PkpDeleteAction(
    _In_ PPK_ACTION Action
    );

PPK_ACTION_SEGMENT PkpAllocateActionSegment(
    _In_ ULONG SegmentIndex
    );

VOID PkpAddToActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ PPK_ACTION Action
    );

// Package

HRESULT PkpCreateSevenZipObject(
    _In_ PGUID ClassId,
    _In_ PGUID InterfaceId,
    _Out_ PVOID *Object
    );

HRESULT PkpWaitForExtractCallback(
    _In_ PkArchiveUpdateCallback *UpdateCallback
    );

#endif
