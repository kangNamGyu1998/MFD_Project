#pragma once

#include <fltKernel.h>
#include <ntstrsafe.h>

#define COMM_PORT_NAME L"\\MFDPort"

#pragma pack( push, 1 )
typedef struct _PROCESS_NAME_RECORD {
    ULONG Pid;
    ULONG ParentPid;
    LIST_ENTRY Entry;
    WCHAR ProcessName[260];
} PROCESS_NAME_RECORD;

LIST_ENTRY g_ProcessNameList;
FAST_MUTEX g_ProcessListLock;

typedef struct _PROC_EVENT_INFO {
    BOOLEAN IsCreate;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ImageName[260];
} PROC_EVENT_INFO, * PPROC_EVENT_INFO;

typedef enum _MESSAGE_TYPE {
    MessageTypeIrpCreate,
    MessageTypeProcEvent
} MESSAGE_TYPE;

typedef struct _CREATE_INFO_CONTEXT {
    BOOLEAN IsPost;
    ULONG Pid;
    ULONG ParentPid;
    WCHAR ProcName[260];
    WCHAR FileName[260];
    NTSTATUS CreateOptions;
    NTSTATUS Status;
} CREATE_INFO_CONTEXT, * PCREATE_INFO_CONTEXT;

typedef struct _GENERIC_MESSAGE {
    MESSAGE_TYPE Type;
    union {
        CREATE_INFO_CONTEXT IrpInfo;
        PROC_EVENT_INFO ProcInfo;
    };
} GENERIC_MESSAGE, * PGENERIC_MESSAGE;
#pragma pack( pop )

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VOID ExtractFileName(const UNICODE_STRING* fullPath, WCHAR* outFileName, SIZE_T outLen);

VOID SaveProcessName(ULONG pid, ULONG parentpid, const WCHAR* InName);

BOOLEAN RemoveProcessName(ULONG pid, WCHAR* OutName, ULONG* OutParentId);
BOOLEAN SearchProcessName(ULONG pid, WCHAR* OutName, ULONG* OutParentId);

extern "C" VOID ProcessNotifyEx(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);

NTSTATUS InstanceSetupCallback(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType);

NTSTATUS PortConnect(PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext, ULONG SizeOfContext, PVOID* ConnectionCookie);

// 포트 연결 해제 콜백
VOID PortDisconnect(PVOID ConnectionCookie);

// IRP_MJ_CREATE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);

// IRP_MJ_CREATE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags);