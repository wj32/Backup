#ifndef CONFIG_H
#define CONFIG_H

#define BK_CONFIG_SECTION_MAP 1
#define BK_CONFIG_SECTION_SOURCE 2
#define BK_CONFIG_SECTION_SOURCEFILTERS 3
#define BK_CONFIG_SECTION_DESTINATION 4

typedef struct _BK_CONFIG
{
    // Map
    PPH_LIST MapFromList;
    PPH_LIST MapToList;

    // Source
    PPH_LIST SourceDirectoryList;
    PPH_LIST SourceFileList;
    ULONG UseShadowCopy;

    // SourceFilters
    PPH_LIST IncludeList;
    PPH_LIST ExcludeList;
    PPH_LIST IncludeSizeList;
    PPH_LIST ExcludeSizeList;

    // Destination
    PPH_STRING DestinationDirectory;
    ULONG CompressionLevel;
    ULONG UseTransactions;
    ULONG Strict;
} BK_CONFIG, *PBK_CONFIG;

NTSTATUS BkCreateConfigFromFile(
    __in PWSTR FileName,
    __out PBK_CONFIG *Config
    );

BOOLEAN BkCreateConfigFromString(
    __in PPH_STRINGREF String,
    __out PBK_CONFIG *Config
    );

VOID BkDereferenceStringList(
    __in PPH_LIST List
    );

VOID BkFreeConfig(
    __in PBK_CONFIG Config
    );

#endif
