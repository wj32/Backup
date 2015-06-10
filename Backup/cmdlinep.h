#ifndef CMDLINEP_H
#define CMDLINEP_H

#define OPTION_CONFIGFILENAME 1
#define OPTION_HELP 2
#define OPTION_REVISIONID 3
#define OPTION_FORCE 4
#define OPTION_TIME 5

BOOLEAN NTAPI CommandLineCallback(
    _In_opt_ PPH_COMMAND_LINE_OPTION Option,
    _In_opt_ PPH_STRING Value,
    _In_opt_ PVOID Context
    );

PPH_STRING GetNtMessage(
    _In_ NTSTATUS Status
    );

PPH_STRING FormatUtcTime(
    _In_ PLARGE_INTEGER Time
    );

VOID RecoverAfterEngineMessages(
    VOID
    );

VOID ConsoleMessageHandler(
    _In_ ULONG Level,
    _In_ _Assume_refs_(1) PPH_STRING Message
    );

VOID EnumDb(
    _In_ PDB_DATABASE Database,
    _In_ PDBF_FILE File,
    _In_opt_ PPH_STRING FileName
    );

VOID PrintHelp(
    _In_opt_ PPH_STRING Command
    );

PPH_STRING FixMultipleBackslashes(
    _In_ PPH_STRINGREF String
    );

#endif
