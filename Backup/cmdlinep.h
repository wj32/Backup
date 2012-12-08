#ifndef CMDLINEP_H
#define CMDLINEP_H

#define OPTION_CONFIGFILENAME 1
#define OPTION_HELP 2
#define OPTION_REVISIONID 3
#define OPTION_FORCE 4
#define OPTION_TIME 5

BOOLEAN NTAPI CommandLineCallback(
    __in_opt PPH_COMMAND_LINE_OPTION Option,
    __in_opt PPH_STRING Value,
    __in_opt PVOID Context
    );

PPH_STRING GetNtMessage(
    __in NTSTATUS Status
    );

PPH_STRING FormatUtcTime(
    __in PLARGE_INTEGER Time
    );

VOID RecoverAfterEngineMessages(
    VOID
    );

VOID ConsoleMessageHandler(
    __in ULONG Level,
    __in __assumeRefs(1) PPH_STRING Message
    );

VOID EnumDb(
    __in PDB_DATABASE Database,
    __in PDBF_FILE File,
    __in_opt PPH_STRING FileName
    );

VOID PrintHelp(
    __in_opt PPH_STRING Command
    );

PPH_STRING FixMultipleBackslashes(
    __in PPH_STRINGREF String
    );

#endif
