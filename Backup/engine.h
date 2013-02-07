#ifndef ENGINE_H
#define ENGINE_H

#include "config.h"

#define EN_DATABASE_NAME L"db.bk"

#define EN_MESSAGE_PROGRESS 0
#define EN_MESSAGE_INFORMATION 1
#define EN_MESSAGE_WARNING 2
#define EN_MESSAGE_ERROR 3

typedef VOID (NTAPI *PEN_MESSAGE_HANDLER)(
    __in ULONG Level,
    __in __assumeRefs(1) PPH_STRING Message
    );

NTSTATUS EnQueryRevision(
    __in PBK_CONFIG Config,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PULONGLONG RevisionId,
    __out_opt PLARGE_INTEGER RevisionTimeStamp,
    __out_opt PULONGLONG FirstRevisionId,
    __out_opt PLARGE_INTEGER FirstRevisionTimeStamp
    );

NTSTATUS EnBackupToRevision(
    __in PBK_CONFIG Config,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PULONGLONG RevisionId
    );

NTSTATUS EnTestBackupToRevision(
    __in PBK_CONFIG Config,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnRevertToRevision(
    __in PBK_CONFIG Config,
    __in ULONGLONG TargetRevisionId,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PULONGLONG RevisionId
    );

NTSTATUS EnTrimToRevision(
    __in PBK_CONFIG Config,
    __in ULONGLONG TargetFirstRevisionId,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler,
    __out_opt PULONGLONG FirstRevisionId
    );

#define EN_RESTORE_OVERWRITE_FILES 0x1

NTSTATUS EnRestoreFromRevision(
    __in PBK_CONFIG Config,
    __in ULONG Flags,
    __in PPH_STRINGREF FileName,
    __in_opt ULONGLONG RevisionId,
    __in PPH_STRINGREF RestoreToDirectory,
    __in_opt PPH_STRINGREF RestoreToName,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
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
    __in PBK_CONFIG Config,
    __in PPH_STRINGREF FileName,
    __in PEN_MESSAGE_HANDLER MessageHandler,
    __out PEN_FILE_REVISION_INFORMATION *Entries,
    __out PULONG NumberOfEntries
    );

NTSTATUS EnCompareRevisions(
    __in PBK_CONFIG Config,
    __in ULONGLONG BaseRevisionId,
    __in_opt ULONGLONG TargetRevisionId,
    __in PEN_MESSAGE_HANDLER MessageHandler
    );

NTSTATUS EnCompactDatabase(
    __in PBK_CONFIG Config,
    __in_opt PEN_MESSAGE_HANDLER MessageHandler
    );

#endif
