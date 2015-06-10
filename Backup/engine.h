#ifndef ENGINE_H
#define ENGINE_H

#include "config.h"

#define EN_DATABASE_NAME L"db.bk"

#define EN_MESSAGE_PROGRESS 0
#define EN_MESSAGE_INFORMATION 1
#define EN_MESSAGE_WARNING 2
#define EN_MESSAGE_ERROR 3

typedef VOID (NTAPI *PEN_MESSAGE_HANDLER)(
    _In_ ULONG Level,
    _In_ _Assume_refs_(1) PPH_STRING Message
    );

NTSTATUS EnQueryRevision(
    _In_ PBK_CONFIG Config,
    _In_opt_ PEN_MESSAGE_HANDLER MessageHandler,
    _Out_opt_ PULONGLONG RevisionId,
    _Out_opt_ PLARGE_INTEGER RevisionTimeStamp,
    _Out_opt_ PULONGLONG FirstRevisionId,
    _Out_opt_ PLARGE_INTEGER FirstRevisionTimeStamp
    );

NTSTATUS EnBackupToRevision(
    _In_ PBK_CONFIG Config,
    _In_opt_ PEN_MESSAGE_HANDLER MessageHandler,
    _Out_opt_ PULONGLONG RevisionId
    );

NTSTATUS EnTestBackupToRevision(
    _In_ PBK_CONFIG Config,
    _In_opt_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnRevertToRevision(
    _In_ PBK_CONFIG Config,
    _In_ ULONGLONG TargetRevisionId,
    _In_opt_ PEN_MESSAGE_HANDLER MessageHandler,
    _Out_opt_ PULONGLONG RevisionId
    );

NTSTATUS EnTrimToRevision(
    _In_ PBK_CONFIG Config,
    _In_ ULONGLONG TargetFirstRevisionId,
    _In_opt_ PEN_MESSAGE_HANDLER MessageHandler,
    _Out_opt_ PULONGLONG FirstRevisionId
    );

#define EN_RESTORE_OVERWRITE_FILES 0x1

NTSTATUS EnRestoreFromRevision(
    _In_ PBK_CONFIG Config,
    _In_ ULONG Flags,
    _In_ PPH_STRINGREF FileName,
    _In_opt_ ULONGLONG RevisionId,
    _In_ PPH_STRINGREF RestoreToDirectory,
    _In_opt_ PPH_STRINGREF RestoreToName,
    _In_opt_ PEN_MESSAGE_HANDLER MessageHandler
    );

typedef struct _EN_FILE_REVISION_INFORMATION
{
    ULONG Attributes;
    ULONGLONG RevisionId;
    LARGE_INTEGER TimeStamp;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER LastBackupTime;
} EN_FILE_REVISION_INFORMATION, *PEN_FILE_REVISION_INFORMATION;

NTSTATUS EnQueryFileRevisions(
    _In_ PBK_CONFIG Config,
    _In_ PPH_STRINGREF FileName,
    _In_ PEN_MESSAGE_HANDLER MessageHandler,
    _Out_ PEN_FILE_REVISION_INFORMATION *Entries,
    _Out_ PULONG NumberOfEntries
    );

NTSTATUS EnCompareRevisions(
    _In_ PBK_CONFIG Config,
    _In_ ULONGLONG BaseRevisionId,
    _In_opt_ ULONGLONG TargetRevisionId,
    _In_ PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnCompactDatabase(
    _In_ PBK_CONFIG Config,
    _In_opt_ PEN_MESSAGE_HANDLER MessageHandler
    );

#endif
