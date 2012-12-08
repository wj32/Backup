#ifndef VSSOBJ_H
#define VSSOBJ_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _BK_VSS_OBJECT *PBK_VSS_OBJECT;

HRESULT BkCreateVssObject(
    __out PBK_VSS_OBJECT *Vss
    );

VOID BkDestroyVssObject(
    __in PBK_VSS_OBJECT Vss
    );

HRESULT BkStartSnapshotsVssObject(
    __in PBK_VSS_OBJECT Vss
    );

HRESULT BkAddSnapshotVssObject(
    __in PBK_VSS_OBJECT Vss,
    __in PPH_STRING Volume
    );

HRESULT BkPerformSnapshotsVssObject(
    __in PBK_VSS_OBJECT Vss
    );

PPH_STRING BkMapFileNameVssObject(
    __in PBK_VSS_OBJECT Vss,
    __in PPH_STRING FileName
    );

#ifdef __cplusplus
}
#endif

#endif
