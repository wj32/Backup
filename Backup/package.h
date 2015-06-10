#ifndef PACKAGE_H
#define PACKAGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef PVOID PPK_PACKAGE;

// File stream

typedef PVOID PPK_FILE_STREAM;

PPK_FILE_STREAM PkCreateFileStream(
    _In_opt_ PPH_FILE_STREAM FileStream
    );

VOID PkReferenceFileStream(
    _In_ PPK_FILE_STREAM FileStream
    );

VOID PkDereferenceFileStream(
    _In_ PPK_FILE_STREAM FileStream
    );

// Action list

typedef enum _PK_ACTION_TYPE
{
    PkAddType,
    PkUpdateType,
    PkAddFromPackageType,
    PkMaximumActionType
} PK_ACTION_TYPE;

#define PK_ACTION_DIRECTORY 0x1

typedef struct _PK_ACTION
{
    PK_ACTION_TYPE Type;
    union
    {
        struct
        {
            ULONG Flags;
            PPH_STRING Destination;
        } Add;
        struct
        {
            PPK_PACKAGE Package;
            ULONG IndexInPackage;
        } AddFromPackage;
        struct
        {
            ULONG Index;
        } Update;
    } u;
    PVOID Context;
} PK_ACTION, *PPK_ACTION;

#define PK_ACTION_SEGMENT_SHIFT 6
#define PK_ACTION_SEGMENT_SIZE (1 << PK_ACTION_SEGMENT_SHIFT)

typedef struct _PK_ACTION_SEGMENT
{
    struct _PK_ACTION_SEGMENT *Next;
    ULONG SegmentIndex;
    ULONG Count;
    PK_ACTION Actions[PK_ACTION_SEGMENT_SIZE];
} PK_ACTION_SEGMENT, *PPK_ACTION_SEGMENT;

typedef struct _PK_ACTION_LIST
{
    PPK_ACTION_SEGMENT FirstSegment;
    PPK_ACTION_SEGMENT LastSegment;
    ULONG NumberOfSegments;
    ULONG NumberOfActions;
} PK_ACTION_LIST, *PPK_ACTION_LIST;

PPK_ACTION_LIST PkCreateActionList(
    VOID
    );

VOID PkDestroyActionList(
    _In_ PPK_ACTION_LIST List
    );

ULONG PkQueryCountActionList(
    _In_ PPK_ACTION_LIST List
    );

VOID PkAppendAddToActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ ULONG Flags,
    _In_ PPH_STRING Destination,
    _In_opt_ PVOID Context
    );

VOID PkAppendAddFromPackageToActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ PPK_PACKAGE Package,
    _In_ ULONG IndexInPackage,
    _In_opt_ PVOID Context
    );

VOID PkAppendUpdateToActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ ULONG Index,
    _In_opt_ PVOID Context
    );

PPK_ACTION PkIndexInActionList(
    _In_ PPK_ACTION_LIST List,
    _In_ ULONG Index,
    _Out_opt_ PPK_ACTION_SEGMENT *Segment
    );

PPK_ACTION PkGetNextAction(
    _In_ PPK_ACTION Action,
    _In_ ULONG Index,
    _In_ PPK_ACTION_SEGMENT Segment,
    _Out_opt_ PPK_ACTION_SEGMENT *NewSegment
    );

// Package

typedef enum _PK_PACKAGE_CALLBACK_MESSAGE
{
    PkGetSizeMessage, // PFILE_NETWORK_OPEN_INFORMATION
    PkGetAttributesMessage, // PFILE_NETWORK_OPEN_INFORMATION
    PkGetCreationTimeMessage, // PFILE_NETWORK_OPEN_INFORMATION
    PkGetAccessTimeMessage, // PFILE_NETWORK_OPEN_INFORMATION
    PkGetModifiedTimeMessage, // PFILE_NETWORK_OPEN_INFORMATION
    PkGetStreamMessage, // PPK_PARAMETER_GET_STREAM
    PkFilterItemMessage, // PPK_PARAMETER_FILTER_ITEM
    PkProgressMessage, // PPK_PARAMETER_PROGRESS
    PkMaximumMessage
} PK_PACKAGE_CALLBACK_MESSAGE;

typedef struct _PK_PARAMETER_GET_STREAM
{
    PPK_FILE_STREAM FileStream;
    FILE_NETWORK_OPEN_INFORMATION FileInformation;
} PK_PARAMETER_GET_STREAM, *PPK_PARAMETER_GET_STREAM;

typedef struct _PK_PARAMETER_FILTER_ITEM
{
    BOOLEAN Reject;
    PH_STRINGREF Path;
    ULONG FileAttributes;
    PVOID NewContext;
} PK_PARAMETER_FILTER_ITEM, *PPK_PARAMETER_FILTER_ITEM;

typedef struct _PK_PARAMETER_PROGRESS
{
    ULONGLONG ProgressValue;
    ULONGLONG ProgressTotal;
} PK_PARAMETER_PROGRESS, *PPK_PARAMETER_PROGRESS;

typedef HRESULT (NTAPI *PPK_PACKAGE_CALLBACK)(
    _In_ PK_PACKAGE_CALLBACK_MESSAGE Message,
    _In_opt_ PPK_ACTION Action,
    _In_ PVOID Parameter,
    _In_opt_ PVOID Context
    );

HRESULT PkCreatePackage(
    _In_ PPK_FILE_STREAM FileStream,
    _In_ PPK_ACTION_LIST ActionList,
    _In_ PPK_PACKAGE_CALLBACK Callback,
    _In_opt_ PVOID Context
    );

VOID PkReferencePackage(
    _In_ PPK_PACKAGE Package
    );

VOID PkDereferencePackage(
    _In_ PPK_PACKAGE Package
    );

HRESULT PkOpenPackageWithFilter(
    _In_ PPK_FILE_STREAM FileStream,
    _Inout_opt_ PPK_ACTION_LIST ActionList,
    _In_opt_ PPK_PACKAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ PPK_PACKAGE *Package
    );

HRESULT PkUpdatePackage(
    _In_ PPK_FILE_STREAM FileStream,
    _In_ PPK_PACKAGE Package,
    _In_ PPK_ACTION_LIST ActionList,
    _In_ PPK_PACKAGE_CALLBACK Callback,
    _In_opt_ PVOID Context
    );

HRESULT PkExtractPackage(
    _In_ PPK_PACKAGE Package,
    _In_opt_ PPK_ACTION_LIST ActionList,
    _In_ PPK_PACKAGE_CALLBACK Callback,
    _In_opt_ PVOID Context
    );

#ifdef __cplusplus
}
#endif

#endif
