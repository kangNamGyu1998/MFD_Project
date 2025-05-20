#include <fltKernel.h>
#include <ntstrsafe.h>

#define COMM_PORT_NAME L"\\MFDPort"

PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

// 통신에 사용할 구조체
typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;  // FALSE: Pre, TRUE: Post
    WCHAR FileName[ 260 ];
    NTSTATUS ResultStatus;  // Post에서 사용
} IRP_CREATE_INFO, * PIRP_CREATE_INFO;

//인스턴스 연결 함수
NTSTATUS InstanceSetupCallback( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags,  DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType )
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
    UNREFERENCED_PARAMETER( VolumeDeviceType );
    UNREFERENCED_PARAMETER( VolumeFilesystemType );
    
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] 인스턴스 연결됨\n" );
    
    return STATUS_SUCCESS;
}

// 포트 연결 콜백
NTSTATUS PortConnect( PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext, ULONG SizeOfContext, PVOID* ConnectionCookie )
{
    UNREFERENCED_PARAMETER( ServerPortCookie ); //UNREFERENCED_PARAMETER: 인자값이나 로컬 변수가 아직 선언되지 않았을때 컨파일러 경고를 발생시키지 않게 하기 위한 매크로
    UNREFERENCED_PARAMETER( ConnectionContext );
    UNREFERENCED_PARAMETER( SizeOfContext );
    UNREFERENCED_PARAMETER( ConnectionCookie );

    gClientPort = ClientPort;
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] 포트 연결됨\n" );
    return STATUS_SUCCESS;
}

// 포트 연결 해제 콜백
VOID PortDisconnect( PVOID ConnectionCookie )
{
    UNREFERENCED_PARAMETER( ConnectionCookie );
    //UserConsole과 통신 포트가 연결 되었다면 실행
    if( gClientPort ) {
        FltCloseClientPort( gFilterHandle, &gClientPort ); //연결된 포트 닫기
        gClientPort = NULL; //통신 포트 초기화
    }
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] 포트 연결 해제\n" );
}

// IRP_MJ_CREATE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext )
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    if( !gClientPort ) //UserConsole과 통신 포트가 연결되지 않은 경우 메세지를 보낼 수 없으므로 종료
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    IRP_CREATE_INFO info = { 0 };
    info.IsPost = FALSE; //Pre라는 것을 표시

    PFLT_FILE_NAME_INFORMATION nameInfo;
    //현재 요청된 파일의 경로 정보를 가져옴. FLT_FILE_NAME_NORMALIZED: 정규화된 파일 경로, FLT_FILE_NAME_QUERY_DEFAULT: 시스템이 판단한 파일 이름.
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo ); //nameInfo 구조체의 멤버들을 파싱해서 .Volume, .FinalComponent, .Extension 등으로 분리.
    	RtlStringCchCopyW( info.FileName, 260, nameInfo->Name.Buffer ); //안전하게 문자열을 복사, 260은 최대 경로 길이(MAX_PATH)
        FltReleaseFileNameInformation( nameInfo ); //FltGetFilaNameInformation으로 할당된 메모리 해제
    }
    else {
        RtlStringCchCopyW( info.FileName, 260, L"<Unknown>" );
    }

    // 드라이버에서 UserConsole로 메시지를 전송. &info(IRP_CREATE_INFO): 전송할 데이터 구조체. UserConsole에서 FilterGetMessage()로 수신
    FltSendMessage( gFilterHandle, &gClientPort, &info, sizeof( info ), NULL, NULL, NULL );

    return FLT_PREOP_SUCCESS_WITH_CALLBACK; //이 요청에 대해 추가 처리를 하지 않겠다고 반환
}

// IRP_MJ_CREATE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags )
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );
    UNREFERENCED_PARAMETER( Flags );

    if( !gClientPort ) //UserConsole과 통신 포트가 연결되지 않은 경우 메세지를 보낼 수 없으므로 종료
        return FLT_POSTOP_FINISHED_PROCESSING;

    IRP_CREATE_INFO info = { 0 };
    info.IsPost = TRUE;
    info.ResultStatus = Data->IoStatus.Status; //ResultStatus에 IRP처리 결과를 저장 (ex. STATUS_SUCCESS, STATUS_ACCESS_DENIED 등)

    PFLT_FILE_NAME_INFORMATION nameInfo;
    //현재 요청된 파일의 경로 정보를 가져옴. FLT_FILE_NAME_NORMALIZED: 정규화된 파일 경로, FLT_FILE_NAME_QUERY_DEFAULT: 시스템이 판단한 파일 이름.
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo ); //nameInfo 구조체의 멤버들을 파싱해서 .Volume, .FinalComponent, .Extension 등으로 분리.
        RtlStringCchCopyW( info.FileName, 260, nameInfo->Name.Buffer ); //안전하게 문자열을 복사, 260은 최대 경로 길이(MAX_PATH)
        FltReleaseFileNameInformation( nameInfo ); //FltGetFilaNameInformation으로 할당된 메모리 해제
    }
    else {
        RtlStringCchCopyW( info.FileName, 260, L"<Unknown>" );
    }

    // 드라이버에서 UserConsole로 메시지를 전송. &info(IRP_CREATE_INFO): 전송할 데이터 구조체. UserConsole에서 FilterGetMessage()로 수신
    FltSendMessage( gFilterHandle, &gClientPort, &info, sizeof( info ), NULL, NULL, NULL ); 

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
    UNREFERENCED_PARAMETER( Flags );

    if( gServerPort )
        FltCloseCommunicationPort( gServerPort );

    if( gFilterHandle )
        FltUnregisterFilter( gFilterHandle );

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "드라이버 언로딩 완료\n" );
    return STATUS_SUCCESS;
}

// Entry Point
extern "C"
NTSTATUS DriverEntry( PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath )
{
    UNREFERENCED_PARAMETER( RegistryPath );
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
    if( !NT_SUCCESS( status ) ) {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltRegisterFilter 실패: 0x%X\n", status );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltRegisterFilter 성공\n" );

    UNICODE_STRING uniName = RTL_CONSTANT_STRING( COMM_PORT_NAME );
    OBJECT_ATTRIBUTES oa;
    PSECURITY_DESCRIPTOR sd = NULL;

    status = FltBuildDefaultSecurityDescriptor( &sd, FLT_PORT_ALL_ACCESS );
    if( !NT_SUCCESS( status ) ) {
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

    if( !NT_SUCCESS( status ) ) {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltCreateCommunicationPort 실패: 0x%X\n", status );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltCreateCommunicationPort 성공\n" );

    status = FltStartFiltering( gFilterHandle );
    if( !NT_SUCCESS( status ) ) {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltStartFiltering 실패: 0x%X\n", status );
        FltCloseCommunicationPort( gServerPort );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltStartFiltering 성공 - 드라이버 로딩 완료\n" );

    return STATUS_SUCCESS;
}