#include <fltKernel.h>
#include <ntstrsafe.h>

#define COMM_PORT_NAME L"\\MFDPort"

PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

LARGE_INTEGER TimeOut;

#pragma pack(push, 1)
typedef struct _PROCESS_NAME_RECORD {
    ULONG Pid;
    LIST_ENTRY Entry;
    WCHAR ProcessName[ 260 ];
} PROCESS_NAME_RECORD;

LIST_ENTRY g_ProcessNameList;
FAST_MUTEX g_ProcessListLock;

typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;
    NTSTATUS ResultStatus;
    WCHAR ImageName[ 260 ];
} IRP_CREATE_INFO, * PIRP_CREATE_INFO;

typedef struct _PROC_EVENT_INFO {
    BOOLEAN IsCreate;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ImageName[ 260 ];
} PROC_EVENT_INFO, * PPROC_EVENT_INFO;

typedef enum _MESSAGE_TYPE {
    MessageTypeIrpCreate,
    MessageTypeProcEvent
} MESSAGE_TYPE;

typedef struct _GENERIC_MESSAGE {
    MESSAGE_TYPE Type;
    union {
        IRP_CREATE_INFO IrpInfo;
        PROC_EVENT_INFO ProcInfo;
    };
} GENERIC_MESSAGE, * PGENERIC_MESSAGE;
#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VOID ExtractFileName( const WCHAR* fullPath, WCHAR* outFileName, SIZE_T outLen )
{
    if( fullPath == NULL || outFileName == NULL )
        return;

    const WCHAR* lastSlash = fullPath;
    const WCHAR* p = fullPath;

    while( *p != L'\0' ) {
        if( *p == L'\\' || *p == L'/' )
            lastSlash = p + 1;
        p++;
    }

    RtlStringCchCopyW( outFileName, outLen, lastSlash );
}

VOID SaveProcessName( ULONG pid, const WCHAR* InName ) {
    PROCESS_NAME_RECORD* rec = (PROCESS_NAME_RECORD*)ExAllocatePoolWithTag( NonPagedPool, sizeof( PROCESS_NAME_RECORD ), 'prnm' );
	if( rec == NULL )
        return;

    rec->Pid = pid; //���� �Ҵ�� �޸𸮿� PID ����
    RtlStringCchCopyW( rec->ProcessName, 260, InName );

    ExAcquireFastMutex( &g_ProcessListLock );
    InsertTailList( &g_ProcessNameList, &rec->Entry );
    ExReleaseFastMutex( &g_ProcessListLock );
}

BOOLEAN FindProcessName( ULONG pid, WCHAR* OutName ) {
    BOOLEAN found = FALSE;

    ExAcquireFastMutex( &g_ProcessListLock );

    for( PLIST_ENTRY p = g_ProcessNameList.Flink; p != &g_ProcessNameList; p = p->Flink ) {
        PROCESS_NAME_RECORD* rec = CONTAINING_RECORD( p, PROCESS_NAME_RECORD, Entry );
        if( rec->Pid == pid ) {
            RtlStringCchCopyW( OutName, 260, rec->ProcessName );
            RemoveEntryList( p );
            ExFreePoolWithTag( rec, 'prnm' );
            found = TRUE;
            break;
        }
    }

    ExReleaseFastMutex( &g_ProcessListLock );
    return found;
}

extern "C"
VOID ProcessNotifyEx( PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo )
{
    UNREFERENCED_PARAMETER( Process );

    if( gClientPort == NULL )
        return;

    GENERIC_MESSAGE msg = {};
    msg.Type = MessageTypeProcEvent;
    TimeOut.QuadPart = -10 * 1000 * 1000;

    if( CreateInfo != NULL && CreateInfo->ImageFileName->Length< 260 * sizeof(WCHAR) ) {
        msg.ProcInfo.IsCreate = TRUE;
        msg.ProcInfo.ProcessId = (ULONG)(ULONG_PTR)ProcessId;
        msg.ProcInfo.ParentProcessId = (ULONG)(ULONG_PTR)CreateInfo->ParentProcessId;

        if( CreateInfo->ImageFileName != NULL ) 
        {
            WCHAR ShortName[260] = L"<Unknown>";
            ExtractFileName( CreateInfo->ImageFileName->Buffer, ShortName, 259 );
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 259, ShortName );
            SaveProcessName( (ULONG)(ULONG_PTR)ProcessId, ShortName );
        }
        else
        {
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, L"<Unknown>" );
        }
    }
    else {
        msg.ProcInfo.IsCreate = FALSE;
        msg.ProcInfo.ProcessId = (ULONG)(ULONG_PTR)ProcessId;

        WCHAR TName[ 260 ] = L"<Unknown>";
        if( FindProcessName( msg.ProcInfo.ProcessId, TName ) )
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, TName );
        else
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, L"<Unknown Process>" );
    }

    FltSendMessage( gFilterHandle, &gClientPort, &msg, sizeof( GENERIC_MESSAGE ), NULL, NULL, &TimeOut );
}

NTSTATUS InstanceSetupCallback( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType )
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeDeviceType);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    return STATUS_SUCCESS;
}

NTSTATUS PortConnect( PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext, ULONG SizeOfContext, PVOID* ConnectionCookie )
{
    gClientPort = ClientPort;
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] 포트 연결됨\n" );
    return STATUS_SUCCESS;
}

// 포트 연결 해제 콜백
VOID PortDisconnect( PVOID ConnectionCookie )
{
    if( gClientPort != NULL ) {
        FltCloseClientPort( gFilterHandle, &gClientPort );
        gClientPort = NULL;
    }
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[-] 포트 연결 해제\n" );
}

// IRP_MJ_CREATE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext )
{
    if( gClientPort == NULL )
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    GENERIC_MESSAGE MSG = {};
    MSG.Type = MessageTypeIrpCreate;
    MSG.IrpInfo.IsPost = FALSE;

    PFLT_FILE_NAME_INFORMATION nameInfo;
    WCHAR ShortName[260] = L"<Unknown>";
    
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo );
        ExtractFileName(nameInfo->Name.Buffer, ShortName, 260);
        RtlStringCchCopyW( MSG.IrpInfo.ImageName, 260, ShortName);
        FltReleaseFileNameInformation( nameInfo );
    }
    else {
        RtlStringCchCopyW( MSG.IrpInfo.ImageName, 260, L"<Unknown>" );
    }

    FltSendMessage( gFilterHandle, &gClientPort, &MSG, sizeof( MSG ), NULL, NULL, NULL );

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

// IRP_MJ_CREATE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags )
{
    if( gClientPort == NULL )
        return FLT_POSTOP_FINISHED_PROCESSING;

    GENERIC_MESSAGE MSG = {};
    MSG.Type = MessageTypeIrpCreate;
	MSG.IrpInfo.IsPost = TRUE;
    MSG.IrpInfo.ResultStatus = Data->IoStatus.Status;

    PFLT_FILE_NAME_INFORMATION nameInfo;
    WCHAR ShortName[260] = L"<Unknown>";
 
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo );
        ExtractFileName(nameInfo->Name.Buffer, ShortName, 260);
        RtlStringCchCopyW( MSG.IrpInfo.ImageName, 260, ShortName );
        FltReleaseFileNameInformation( nameInfo );
    }
    else {
        RtlStringCchCopyW( MSG.IrpInfo.ImageName, 260, L"<Unknown>" );
    }

    FltSendMessage( gFilterHandle, &gClientPort, &MSG, sizeof( MSG ), NULL, NULL, NULL );

    return FLT_POSTOP_FINISHED_PROCESSING;
}

// Operation Registration
CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE, 0, PreCreateCallback, PostCreateCallback },
    { IRP_MJ_OPERATION_END }
};

// Unload
NTSTATUS DriverUnload( FLT_FILTER_UNLOAD_FLAGS Flags )
{
    if( gServerPort != NULL )
        FltCloseCommunicationPort( gServerPort );

    if( gFilterHandle != NULL )
        FltUnregisterFilter( gFilterHandle );

    PsSetCreateProcessNotifyRoutineEx( ProcessNotifyEx, TRUE );
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[-] 드라이버 언로딩 완료\n" );
    return STATUS_SUCCESS;
}

// Entry Point
extern "C"
NTSTATUS DriverEntry( PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath )
{
    NTSTATUS status;

    FLT_REGISTRATION filterRegistration = {
        sizeof( FLT_REGISTRATION ),
        FLT_REGISTRATION_VERSION,
        0,
        NULL,
        Callbacks,
        DriverUnload,
        InstanceSetupCallback,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    };

    status = FltRegisterFilter( DriverObject, &filterRegistration, &gFilterHandle );
    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltRegisterFilter 실패: 0x%X\n", status );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltRegisterFilter 성공\n" );

    UNICODE_STRING uniName = RTL_CONSTANT_STRING( COMM_PORT_NAME );
    OBJECT_ATTRIBUTES oa;
    PSECURITY_DESCRIPTOR sd = NULL;

    status = FltBuildDefaultSecurityDescriptor( &sd, FLT_PORT_ALL_ACCESS );
    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "SecurityDescriptor 생성 실패: 0x%X\n", status );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    InitializeObjectAttributes( &oa, &uniName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, sd );

    status = FltCreateCommunicationPort(
        gFilterHandle,
        &gServerPort,
        &oa,
        NULL,
        PortConnect,
        PortDisconnect,
        NULL,
        1
    );

    FltFreeSecurityDescriptor( sd );

    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltCreateCommunicationPort 실패: 0x%X\n", status );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltCreateCommunicationPort 성공\n" );

    status = FltStartFiltering( gFilterHandle );
    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltStartFiltering 실패: 0x%X\n", status );
        FltCloseCommunicationPort( gServerPort );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    ExInitializeFastMutex( &g_ProcessListLock );
    InitializeListHead( &g_ProcessNameList );

    status = PsSetCreateProcessNotifyRoutineEx( ProcessNotifyEx, FALSE );
    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "ProcessNotify 등록 실패: 0x%X\n", status );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltStartFiltering 성공 - 드라이버 로딩 완료\n" );

    return STATUS_SUCCESS;
}