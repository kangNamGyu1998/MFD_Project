#pragma once

#include <fltKernel.h>
#include <ntstrsafe.h>
#include <ntifs.h>

#define COMM_PORT_NAME L"\\MFDPort"

#pragma pack( push, 1 )
typedef struct _PROCESS_NAME_RECORD {
    ULONG Pid;
    ULONG ParentPid;
    LIST_ENTRY Entry;
    WCHAR ProcessName[ 260 ];
} PROCESS_NAME_RECORD;

LIST_ENTRY g_ProcessNameList;
FAST_MUTEX g_ProcessListLock;

typedef struct _PROC_EVENT_INFO {
    BOOLEAN IsCreate;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ImageName[ 260 ];
} PROC_EVENT_INFO, * PPROC_EVENT_INFO;

typedef enum _MESSAGE_TYPE {
    MessageTypeIrpCreate,
    MessageTypeIrpCleanup,
    MessageTypeIrpClose
} MESSAGE_TYPE;

typedef struct _IRP_CONTEXT {
    BOOLEAN IsPost;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ProcName[ 260 ];
    WCHAR FileName[ 260 ];
    ULONG CreateOptions;
    NTSTATUS ResultStatus;
} IRP_CONTEXT, * PIRP_CONTEXT;

typedef struct _GENERIC_MESSAGE {
    MESSAGE_TYPE Type;
    union {
        IRP_CONTEXT IrpInfo;
        PROC_EVENT_INFO ProcInfo;
    };
} GENERIC_MESSAGE, * PGENERIC_MESSAGE;

typedef struct _SNAP_CTX {
    ULONG  Pid, Ppid;
    WCHAR  Proc[260];
    WCHAR  File[260];
} SNAP_CTX, * PSNAP_CTX;

struct { ULONG Flag; const wchar_t* Name; } flags[] = {
        { 0x00000001, L"FILE_DIRECTORY_FILE" },
        { 0x00000002, L"FILE_WRITE_THROUGH" },
        { 0x00000004, L"FILE_SEQUENTIAL_ONLY" },
        { 0x00000008, L"FILE_NO_INTERMEDIATE_BUFFERING" },
        { 0x00000010, L"FILE_SYNCHRONOUS_IO_ALERT" },
        { 0x00000020, L"FILE_SYNCHRONOUS_IO_NONALERT" },
        { 0x00000040, L"FILE_NON_DIRECTORY_FILE" },
        { 0x00000080, L"FILE_CREATE_TREE_CONNECTION" },
        { 0x00000100, L"FILE_COMPLETE_IF_OPLOCKED" },
        { 0x00000200, L"FILE_NO_EA_KNOWLEDGE" },
        { 0x00000400, L"FILE_OPEN_REMOTE_INSTANCE" },
        { 0x00000800, L"FILE_RANDOM_ACCESS" },
        { 0x00001000, L"FILE_DELETE_ON_CLOSE" },
        { 0x00002000, L"FILE_OPEN_BY_FILE_ID" },
        { 0x00004000, L"FILE_OPEN_FOR_BACKUP_INTENT" },
        { 0x00008000, L"FILE_NO_COMPRESSION" },
        { 0x00010000, L"FILE_OPEN_REQUIRING_OPLOCK" },
        { 0x00020000, L"FILE_DISALLOW_EXCLUSIVE" },
        { 0x00040000, L"FILE_SESSION_AWARE" },
        { 0x00080000, L"FILE_RESERVE_OPFILTER" }
};

typedef struct _MY_FILE_CONTEXT {
    WCHAR FileName[260];
    WCHAR ProcName[260];
    ULONG ProcessId;
    ULONG ParentProcessId;
} MY_FILE_CONTEXT, * PMY_FILE_CONTEXT;

NTSTATUS SetFileContextFromCreate( 
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    const WCHAR* FileName,
    const WCHAR* ProcName,
    ULONG Pid,
    ULONG Ppid
 );

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

typedef struct _MY_STREAMHANDLE_CONTEXT {
    WCHAR FileName[260];
    WCHAR ProcName[260];
    ULONG ProcessId;
    ULONG ParentProcessId;
} MY_STREAMHANDLE_CONTEXT, * PMY_STREAMHANDLE_CONTEXT;

NTSTATUS CreateStreamHandleContext( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects );
NTSTATUS QueryStreamHandleContext( PFLT_INSTANCE Instance, PFILE_OBJECT FileObject, PMY_STREAMHANDLE_CONTEXT* OutContext );

PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

LARGE_INTEGER TimeOut;
#pragma pack( pop )

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VOID ExtractFileName( const UNICODE_STRING* fullPath, WCHAR* outFileName, SIZE_T outLen );
VOID SaveProcessName( ULONG pid, ULONG parentpid, const WCHAR* InName );

NTSTATUS SearchProcessInfo( ULONG pid, WCHAR* OutName, ULONG* OutParentId );
NTSTATUS InstanceSetupCallback( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType );
NTSTATUS PortConnect( PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext, ULONG SizeOfContext, PVOID* ConnectionCookie );
// 포트 연결 해제 콜백
VOID PortDisconnect( PVOID ConnectionCookie );
//컨텍스트 클린업 콜백
VOID FileContextCleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType);
// IRP_MJ_CREATE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext );
// IRP_MJ_CREATE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags );
// IRP_MJ_CLEANUP PreCallback
FLT_PREOP_CALLBACK_STATUS PreCleanupCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext );
// IRP_MJ_CLEANUP PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCleanupCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags );
// IRP_MJ_CLOSE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCloseCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext );
// IRP_MJ_CLOSE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCloseCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags );

extern "C" HANDLE PsGetProcessInheritedFromUniqueProcessId( PEPROCESS Process );
extern "C" VOID ProcessNotifyEx( PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo );
extern const FLT_CONTEXT_REGISTRATION g_ContextRegs[];