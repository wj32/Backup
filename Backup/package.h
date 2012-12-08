#ifndef PACKAGE_H
#define PACKAGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef PVOID PPK_PACKAGE;

// File stream

typedef PVOID PPK_FILE_STREAM;

PPK_FILE_STREAM PkCreateFileStream(
    __in_opt PPH_FILE_STREAM FileStream
    );

VOID PkReferenceFileStream(
    __in PPK_FILE_STREAM FileStream
    );

VOID PkDereferenceFileStream(
    __in PPK_FILE_STREAM FileStream
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
    __in PPK_ACTION_LIST List
    );

ULONG PkQueryCountActionList(
    __in PPK_ACTION_LIST List
    );

VOID PkAppendAddToActionList(
    __in PPK_ACTION_LIST List,
    __in ULONG Flags,
    __in PPH_STRING Destination,
    __in_opt PVOID Context
    );

VOID PkAppendAddFromPackageToActionList(
    __in PPK_ACTION_LIST List,
    __in PPK_PACKAGE Package,
    __in ULONG IndexInPackage,
    __in_opt PVOID Context
    );

VOID PkAppendUpdateToActionList(
    __in PPK_ACTION_LIST List,
    __in ULONG Index,
    __in_opt PVOID Context
    );

PPK_ACTION PkIndexInActionList(
    __in PPK_ACTION_LIST List,
    __in ULONG Index,
    __out_opt PPK_ACTION_SEGMENT *Segment
    );

PPK_ACTION PkGetNextAction(
    __in PPK_ACTION Action,
    __in ULONG Index,
    __in PPK_ACTION_SEGMENT Segment,
    __out_opt PPK_ACTION_SEGMENT *NewSegment
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
    __in PK_PACKAGE_CALLBACK_MESSAGE Message,
    __in_opt PPK_ACTION Action,
    __in PVOID Parameter,
    __in_opt PVOID Context
    );

HRESULT PkCreatePackage(
    __in PPK_FILE_STREAM FileStream,
    __in PPK_ACTION_LIST ActionList,
    __in PPK_PACKAGE_CALLBACK Callback,
    __in_opt PVOID Context
    );

VOID PkReferencePackage(
    __in PPK_PACKAGE Package
    );

VOID PkDereferencePackage(
    __in PPK_PACKAGE Package
    );

HRESULT PkOpenPackageWithFilter(
    __in PPK_FILE_STREAM FileStream,
    __inout_opt PPK_ACTION_LIST ActionList,
    __in_opt PPK_PACKAGE_CALLBACK Callback,
    __in_opt PVOID Context,
    __out PPK_PACKAGE *Package
    );

HRESULT PkUpdatePackage(
    __in PPK_FILE_STREAM FileStream,
    __in PPK_PACKAGE Package,
    __in PPK_ACTION_LIST ActionList,
    __in PPK_PACKAGE_CALLBACK Callback,
    __in_opt PVOID Context
    );

HRESULT PkExtractPackage(
    __in PPK_PACKAGE Package,
    __in_opt PPK_ACTION_LIST ActionList,
    __in PPK_PACKAGE_CALLBACK Callback,
    __in_opt PVOID Context
    );

#ifdef __cplusplus
}
#endif

#endif
