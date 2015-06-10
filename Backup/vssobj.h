#ifndef VSSOBJ_H
#define VSSOBJ_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _BK_VSS_OBJECT *PBK_VSS_OBJECT;

HRESULT BkCreateVssObject(
    _Out_ PBK_VSS_OBJECT *Vss
    );

VOID BkDestroyVssObject(
    _In_ PBK_VSS_OBJECT Vss
    );

HRESULT BkStartSnapshotsVssObject(
    _In_ PBK_VSS_OBJECT Vss
    );

HRESULT BkAddSnapshotVssObject(
    _In_ PBK_VSS_OBJECT Vss,
    _In_ PPH_STRING Volume
    );

HRESULT BkPerformSnapshotsVssObject(
    _In_ PBK_VSS_OBJECT Vss
    );

PPH_STRING BkMapFileNameVssObject(
    _In_ PBK_VSS_OBJECT Vss,
    _In_ PPH_STRING FileName
    );

#ifdef __cplusplus
}
#endif

#endif
