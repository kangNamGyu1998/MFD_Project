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

typedef struct _IRP_CONTEXT {
    BOOLEAN IsPost;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ProcName[260];
    WCHAR FileName[260];
    NTSTATUS CreateOptions;
    NTSTATUS ResultStatus;
} IRP_CONTEXT, * PIRP_CONTEXT;

typedef struct _GENERIC_MESSAGE {
    MESSAGE_TYPE Type;
    union {
        IRP_CONTEXT IrpInfo;
        PROC_EVENT_INFO ProcInfo;
    };
} GENERIC_MESSAGE, * PGENERIC_MESSAGE;
#pragma pack( pop )

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VOID ExtractFileName(const UNICODE_STRING* fullPath, WCHAR* outFileName, SIZE_T outLen);

VOID SaveProcessName(ULONG pid, ULONG parentpid, const WCHAR* InName);

BOOLEAN RemoveProcessName(ULONG pid, WCHAR* OutName, ULONG* OutParentId);
extern "C" VOID ProcessNotifyEx(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);

NTSTATUS InstanceSetupCallback(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType);

NTSTATUS PortConnect(PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext, ULONG SizeOfContext, PVOID* ConnectionCookie);

// 포트 연결 해제 콜백
VOID PortDisconnect(PVOID ConnectionCookie);

// IRP_MJ_CREATE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);

// IRP_MJ_CREATE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags);

enum TyEnBufferType
{
    BUFFER_UNKNOWN,
    BUFFER_IRPCONTEXT,

    BUFFER_FILENAME,
    BUFFER_PROCNAME,

    BUFFER_MSG_SEND,
    BUFFER_MSG_REPLY,

    ///////////////////////////////////////////////////////////////////////////

    BUFFER_SWAP_READ = 100,
    BUFFER_SWAP_READ_1024,      // 직접 지정하지 말 것
    BUFFER_SWAP_READ_4096,      // 직접 지정하지 말 것
    BUFFER_SWAP_READ_8192,      // 직접 지정하지 말 것
    BUFFER_SWAP_READ_16384,     // 직접 지정하지 말 것
    BUFFER_SWAP_READ_65536,     // 직접 지정하지 말 것

    BUFFER_SWAP_WRITE = 200,
    BUFFER_SWAP_WRITE_1024,     // 직접 지정하지 말 것
    BUFFER_SWAP_WRITE_4096,     // 직접 지정하지 말 것
    BUFFER_SWAP_WRITE_8192,     // 직접 지정하지 말 것
    BUFFER_SWAP_WRITE_16384,    // 직접 지정하지 말 것
    BUFFER_SWAP_WRITE_65536,    // 직접 지정하지 말 것
};

struct TyBaseBuffer
{
    TyEnBufferType      BufferType;
    BOOLEAN             IsAllocatedFromLookasideList;
    SIZE_T              BufferSize;
    ULONG               PoolTag;

    TyBaseBuffer() : BufferType(BUFFER_UNKNOWN)
        , IsAllocatedFromLookasideList(FALSE)
        , BufferSize(0)
    {
    }
};

template< typename T >
struct TyGenericBuffer : public TyBaseBuffer
{
    T* Buffer;

    TyGenericBuffer<T>() : TyBaseBuffer(), Buffer(nullptr)
    {
    }

};