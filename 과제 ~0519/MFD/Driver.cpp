#include <fltKernel.h>
#include <ntstrsafe.h>

#define COMM_PORT_NAME L"\\MFDPort"

PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

//Terminate된 프로세스의 이름과 PID를 저장하기 위한 구조체
typedef struct _PROCESS_NAME_RECORD {
    ULONG Pid;
    WCHAR ProcessName[ 260 ];
    LIST_ENTRY Entry;
} PROCESS_NAME_RECORD;

LIST_ENTRY g_ProcessNameList;
FAST_MUTEX g_ProcessListLock;

// IRP 통신에 사용할 구조체
typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;
    WCHAR FileName[ 260 ];
    NTSTATUS ResultStatus;
} IRP_CREATE_INFO, * PIRP_CREATE_INFO;

//Process Event 통신에 사용할 구조체
typedef struct _PROC_EVENT_INFO {
    BOOLEAN IsCreate;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ImageName[ 260 ];
} PROC_EVENT_INFO, * PPROC_EVENT_INFO;

typedef struct _GENERIC_MESSAGE {
    union {
        IRP_CREATE_INFO IrpInfo;
        PROC_EVENT_INFO ProcInfo;
    };
} GENERIC_MESSAGE, * PGENERIC_MESSAGE;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//CreateInfo시 Terminate를 대비해서 Pid와 ImangeName을 저장하는 함수
VOID SaveProcessName( ULONG pid, const WCHAR* InName ) {
    //동적 할당을 하며 할당 실패 시 함수를 바로 종료해 안정성을 확보한다, NonPagePool: 항상 메모리에 상주하는 커널 풀, prnm: 메모리 태그
    PROCESS_NAME_RECORD* rec = (PROCESS_NAME_RECORD*)ExAllocatePoolWithTag( NonPagedPool, sizeof( PROCESS_NAME_RECORD ), 'prnm' );
	if( !rec ) return;

    rec->Pid = pid; //동적 할당된 메모리에 PID 저장
    RtlStringCchCopyW( rec->ProcessName, 260, InName ); //동적 할당된 메모리에 ImageName 저장

    ExAcquireFastMutex( &g_ProcessListLock ); //뮤텍스로 동기화하여 다중 스레드 접근 충돌 방지
    InsertTailList( &g_ProcessNameList, &rec->Entry ); //리스트 끝에 새로운 항목 추가
    ExReleaseFastMutex( &g_ProcessListLock ); //뮤텍스 동기화 해제
}

BOOLEAN FindProcessName( ULONG pid, WCHAR* OutName ) {
    BOOLEAN found = FALSE;

    ExAcquireFastMutex( &g_ProcessListLock ); //뮤덱스로 동기화하여 다중 스레드 접근 충돌 방지

    for( PLIST_ENTRY p = g_ProcessNameList.Flink; p != &g_ProcessNameList; p = p->Flink ) {
        PROCESS_NAME_RECORD* rec = CONTAINING_RECORD( p, PROCESS_NAME_RECORD, Entry );
        if( rec->Pid == pid ) {
            RtlStringCchCopyW( OutName, 260, rec->ProcessName ); //리스트 순회 중 Pid가 기록된 Pid와 같다면 그 구조체의 ProcessName을 OutName에 복사
            RemoveEntryList( p ); //리시트에서 해당 구조체 제거
            ExFreePoolWithTag( rec, 'prnm' ); //메모리 해제
            found = TRUE;
            break;
        }
    }

    ExReleaseFastMutex( &g_ProcessListLock );
    return found;
}

//프로세스 생성/종료를 알려주는 함수
extern "C"
VOID ProcessNotifyEx( PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo )
{
    UNREFERENCED_PARAMETER( Process );

    if( !gClientPort )
        return;

    GENERIC_MESSAGE msg = { 0 };

    if( CreateInfo ) {
        // 프로세스 생성
        msg.ProcInfo.IsCreate = TRUE;
        msg.ProcInfo.ProcessId = (ULONG)(ULONG_PTR)ProcessId;
        msg.ProcInfo.ParentProcessId = (ULONG)(ULONG_PTR)CreateInfo->ParentProcessId;

        if( CreateInfo->ImageFileName ) {
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, CreateInfo->ImageFileName->Buffer );
            SaveProcessName( msg.ProcInfo.ProcessId, msg.ProcInfo.ImageName );
        }
        else {
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, L"<Unknown>" );
        }
    }
    else {
        // 프로세스 종료
        msg.ProcInfo.IsCreate = FALSE;
        msg.ProcInfo.ProcessId = (ULONG)(ULONG_PTR)ProcessId;

        WCHAR TName[ 260 ] = L"<Unknown>";
        if( FindProcessName( msg.ProcInfo.ProcessId, TName ) )
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, TName );
        else
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, L"<Unknown Terminate>" );
    }

    FltSendMessage( gFilterHandle, &gClientPort, &msg, sizeof( msg ), NULL, NULL, NULL );
}

//인스턴스 연결 함수
NTSTATUS InstanceSetupCallback( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType )
{
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] 인스턴스 연결됨\n" );

    return STATUS_SUCCESS;
}

// 포트 연결 콜백
NTSTATUS PortConnect( PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext, ULONG SizeOfContext, PVOID* ConnectionCookie )
{
    gClientPort = ClientPort;
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] 포트 연결됨\n" );
    return STATUS_SUCCESS;
}

// 포트 연결 해제 콜백
VOID PortDisconnect( PVOID ConnectionCookie )
{
    //UserConsole과 통신 포트가 연결 되었다면 실행
    if( gClientPort ) {
        FltCloseClientPort( gFilterHandle, &gClientPort ); //연결된 포트 닫기
        gClientPort = NULL; //통신 포트 초기화
    }
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[-] 포트 연결 해제\n" );
}

// IRP_MJ_CREATE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext )
{
    if( !gClientPort ) //UserConsole과 통신 포트가 연결되지 않은 경우 메세지를 보낼 수 없으므로 종료
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    GENERIC_MESSAGE MSG = {0};
    MSG.IrpInfo.IsPost = FALSE; //Pre라는 것을 표시

    PFLT_FILE_NAME_INFORMATION nameInfo;
    //현재 요청된 파일의 경로 정보를 가져옴. FLT_FILE_NAME_NORMALIZED: 정규화된 파일 경로, FLT_FILE_NAME_QUERY_DEFAULT: 시스템이 판단한 파일 이름.
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo ); //nameInfo 구조체의 멤버들을 파싱해서 .Volume, .FinalComponent, .Extension 등으로 분리.
        RtlStringCchCopyW( MSG.IrpInfo.FileName, 260, nameInfo->Name.Buffer ); //안전하게 문자열을 복사, 260은 최대 경로 길이(MAX_PATH)
        FltReleaseFileNameInformation( nameInfo ); //FltGetFilaNameInformation으로 할당된 메모리 해제
    }
    else {
        RtlStringCchCopyW( MSG.IrpInfo.FileName, 260, L"<Unknown>" );
    }

    // 드라이버에서 UserConsole로 메시지를 전송. &info(IRP_CREATE_INFO): 전송할 데이터 구조체. UserConsole에서 FilterGetMessage()로 수신
    //FltSendMessage( gFilterHandle, &gClientPort, &MSG, sizeof( MSG ), NULL, NULL, NULL );

    return FLT_PREOP_SUCCESS_WITH_CALLBACK; //이 요청에 대해 추가 처리를 하지 않겠다고 반환
}

// IRP_MJ_CREATE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags )
{
    if( !gClientPort ) //UserConsole과 통신 포트가 연결되지 않은 경우 메세지를 보낼 수 없으므로 종료
        return FLT_POSTOP_FINISHED_PROCESSING;

    GENERIC_MESSAGE MSG= {0};
	MSG.IrpInfo.IsPost = TRUE;
    MSG.IrpInfo.ResultStatus = Data->IoStatus.Status; //ResultStatus에 IRP처리 결과를 저장 (ex. STATUS_SUCCESS, STATUS_ACCESS_DENIED 등)

    PFLT_FILE_NAME_INFORMATION nameInfo;
    //현재 요청된 파일의 경로 정보를 가져옴. FLT_FILE_NAME_NORMALIZED: 정규화된 파일 경로, FLT_FILE_NAME_QUERY_DEFAULT: 시스템이 판단한 파일 이름.
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo ); //nameInfo 구조체의 멤버들을 파싱해서 .Volume, .FinalComponent, .Extension 등으로 분리.
        RtlStringCchCopyW( MSG.IrpInfo.FileName, 260, nameInfo->Name.Buffer ); //안전하게 문자열을 복사, 260은 최대 경로 길이(MAX_PATH)
        FltReleaseFileNameInformation( nameInfo ); //FltGetFilaNameInformation으로 할당된 메모리 해제
    }
    else {
        RtlStringCchCopyW( MSG.IrpInfo.FileName, 260, L"<Unknown>" );
    }

    // 드라이버에서 UserConsole로 메시지를 전송. &info(IRP_CREATE_INFO): 전송할 데이터 구조체. UserConsole에서 FilterGetMessage()로 수신
    //FltSendMessage( gFilterHandle, &gClientPort, &MSG, sizeof( MSG ), NULL, NULL, NULL );

    return FLT_POSTOP_FINISHED_PROCESSING; //이 요청에 대해 추가 처리를 하지 않겠다고 반환
}

// Operation Registration
CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE, 0, PreCreateCallback, PostCreateCallback },//IRP_MJ_CREATE로는 Pre와 Post 콜백
    { IRP_MJ_OPERATION_END }
};

// Unload
NTSTATUS DriverUnload( FLT_FILTER_UNLOAD_FLAGS Flags )
{
    if( gServerPort )
        FltCloseCommunicationPort( gServerPort );

    if( gFilterHandle )
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

    //필터 등록
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

    // UserConsole 커뮤니케이션 포트 등록
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

    FltFreeSecurityDescriptor( sd ); //Driver와 UserMode 안전 연결

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