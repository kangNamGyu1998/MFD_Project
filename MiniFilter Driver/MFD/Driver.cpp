#include "Driver.hpp"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Create 시점에 FILE_OBJECT에 파일/프로세스 정보를 부착한다.
// ※ REPLACE_IF_EXISTS 사용 이유
//    - 동일 FILE_OBJECT에 이전 컨텍스트가 남아 있더라도 항상 최신 정보로 교체한다.
//    - KEEP_IF_EXISTS를 쓰면 '이미 있으면 그냥 두기'라서 저장 실패(=오래된 정보 재사용) 케이스가 생길 수 있다.
NTSTATUS SetFileContextFromCreate( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, const WCHAR* FileName, const WCHAR* ProcName, ULONG Pid, ULONG Ppid )
{
    UNREFERENCED_PARAMETER( Data );

    NTSTATUS status;
    PMY_FILE_CONTEXT context = NULL;

    // 먼저 이미 컨텍스트가 연결돼 있는지 확인
    status = FltGetFileContext( 
        FltObjects->Instance,
        FltObjects->FileObject,
        ( PFLT_CONTEXT* )&context
    );

    if ( NT_SUCCESS( status ) ) {
        // 이미 있으면 굳이 다시 만들 필요 없음
        FltReleaseContext( context );
        return STATUS_ALREADY_INITIALIZED;
    }

    // 새 FILE_CONTEXT 생성
    status = FltAllocateContext( 
        FltObjects->Filter,
        FLT_FILE_CONTEXT,
        sizeof( MY_FILE_CONTEXT ),
        NonPagedPool,
        ( PFLT_CONTEXT* )&context
    );

    if ( !NT_SUCCESS( status ) )
        return status;

    RtlZeroMemory( context, sizeof( MY_FILE_CONTEXT ) );
    RtlStringCchCopyW( context->FileName, 260, FileName );
    RtlStringCchCopyW( context->ProcName, 260, ProcName );
    context->ProcessId = Pid;
    context->ParentProcessId = Ppid;

    // FILE_CONTEXT를 FileObject에 연결
    status = FltSetFileContext( 
        FltObjects->Instance,
        FltObjects->FileObject,
        FLT_SET_CONTEXT_REPLACE_IF_EXISTS,
        context,
        NULL
    );

    // context는 Set 성공/실패와 무관하게 release 필요
    FltReleaseContext( context );
    return status;
}

// 파일 경로에서 파일 이름만 추출
VOID ExtractFileName( const UNICODE_STRING* fullPath, WCHAR* outFileName, SIZE_T outLen )
{
    if ( fullPath == NULL || outFileName == NULL )
        return;

    USHORT len = fullPath->Length / sizeof( WCHAR );

    // len+1 길이로 할당
    WCHAR* result = ( WCHAR* )ExAllocatePoolWithTag( NonPagedPool, ( len + 1 ) * sizeof( WCHAR ), 'u2wT' );
    if ( result == NULL )
        return;

    RtlZeroMemory( result, ( len + 1 ) * sizeof( WCHAR ) );

    RtlCopyMemory( result, fullPath->Buffer, fullPath->Length );
    result[len] = L'\0';

    const WCHAR* lastSlash = result;
    const WCHAR* p = result;
    while ( *p != L'\0' ) {
        if ( *p == L'\\' || *p == L'/' )
            lastSlash = p + 1;
        ++p;
    }

    RtlStringCchCopyW( outFileName, outLen, lastSlash );
    ExFreePoolWithTag( result, 'u2wT' );
}

// 프로세스 이름과 부모 PID 저장
VOID SaveProcessName( ULONG pid, ULONG parentpid, const WCHAR* InName )
{
    PROCESS_NAME_RECORD* rec = ( PROCESS_NAME_RECORD* )ExAllocatePoolWithTag( NonPagedPool, sizeof( PROCESS_NAME_RECORD ), 'prnm' );
    if ( rec == NULL )
        return;

    RtlZeroMemory( rec, sizeof( *rec ) );

    rec->Pid = pid;
    rec->ParentPid = parentpid;
    RtlStringCchCopyW( rec->ProcessName, 260, InName );

    ExAcquireFastMutex( &g_ProcessListLock );
    InsertTailList( &g_ProcessNameList, &rec->Entry );
    ExReleaseFastMutex( &g_ProcessListLock );
}

//프로세스 이름과 부모 PID 검색
NTSTATUS SearchProcessInfo( ULONG pid, WCHAR* OutName, ULONG* OutParentId )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if ( pid <= 4 )
    {
        if ( OutName != NULL )
            RtlStringCchCopyW( OutName, 260, L"SYSTEM" );

        if ( OutParentId != NULL )
            *OutParentId = 0;

        return STATUS_SUCCESS;
    }

    ExAcquireFastMutex( &g_ProcessListLock );

    for ( PLIST_ENTRY p = g_ProcessNameList.Flink; p != &g_ProcessNameList; p = p->Flink ) 
    {
        PROCESS_NAME_RECORD* rec = CONTAINING_RECORD( p, PROCESS_NAME_RECORD, Entry );
        if ( rec->Pid == pid ) 
        {
            if ( OutName != NULL )
                RtlStringCchCopyW( OutName, 260, rec->ProcessName );
            if ( OutParentId != NULL )
                *OutParentId = rec->ParentPid;

            RemoveEntryList( p );
            ExFreePoolWithTag( rec, 'prnm' );

            ExReleaseFastMutex( &g_ProcessListLock );
            return STATUS_SUCCESS;
        }
    }

    ExReleaseFastMutex( &g_ProcessListLock );

    //리스트에 없을 경우에만 직접 확인 시도
    PEPROCESS process = NULL;
    if ( NT_SUCCESS( PsLookupProcessByProcessId( ( HANDLE )pid, &process ) ) ) {
        PUNICODE_STRING imageName;
        if ( NT_SUCCESS( SeLocateProcessImageName( process, &imageName ) ) ) {
            if ( OutName != NULL )
                ExtractFileName( imageName, OutName, 260 );
            if ( OutParentId != NULL )
            {
                *OutParentId = ( ULONG )PsGetProcessInheritedFromUniqueProcessId( process );
            }
				
            ExFreePool( imageName );
            Status = STATUS_SUCCESS;
        }
        ObDereferenceObject( process );
    }

    return Status;
}

// Process Notify Callback, 프로세스 생성/종료 시 호출
VOID ProcessNotifyEx( PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo )
{
    UNREFERENCED_PARAMETER( Process );

    if ( gClientPort == NULL )
        return;

    GENERIC_MESSAGE msg = {};
    TimeOut.QuadPart = -10 * 1000 * 1000;

    if ( CreateInfo != NULL && CreateInfo->ImageFileName->Length < 260 * sizeof( WCHAR ) )
    {
        msg.ProcInfo.IsCreate = TRUE;
        msg.ProcInfo.ProcessId = ( ULONG )( ULONG_PTR )ProcessId;
        msg.ProcInfo.ParentProcessId = ( ULONG )( ULONG_PTR )CreateInfo->ParentProcessId;

        if ( CreateInfo->ImageFileName != NULL )
        {
            WCHAR ShortName[ 260 ] = L"<Unknown>";
            ExtractFileName( CreateInfo->ImageFileName, ShortName, 260 );
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, ShortName );
            SaveProcessName( ( ULONG )( ULONG_PTR )ProcessId, ( ULONG )( ULONG_PTR )CreateInfo->ParentProcessId, ShortName );
        } 
        else
        {
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, L"<Unknown>" );
        }
    }
    else
    {
        msg.ProcInfo.IsCreate = FALSE;
        msg.ProcInfo.ProcessId = ( ULONG )( ULONG_PTR )ProcessId;

        WCHAR TName[ 260 ] = L"<Unknown>";
        ULONG Ppid = 0;
        if ( NT_SUCCESS(SearchProcessInfo( msg.ProcInfo.ProcessId, TName, &Ppid ) ) )
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, TName );
        else
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, L"<Unknown Process>" );
    }
}

// IRP_MJ_CREATE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext )
{
    TimeOut.QuadPart = -10 * 1000 * 1000;
    if ( gClientPort == NULL )
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    NTSTATUS status;
    PIRP_CONTEXT context = ( PIRP_CONTEXT )ExAllocatePoolWithTag( NonPagedPool, sizeof( IRP_CONTEXT ), 'ctxt' );
    if ( context == NULL )
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    RtlZeroMemory( context, sizeof( IRP_CONTEXT ) );

    // 프로세스 ID
    context->ProcessId = ( ULONG )FltGetRequestorProcessId( Data );

    // 프로세스 이름/부모 PID 가져오기
    WCHAR procName[260] = L"<Unknown>";
    ULONG parentPid = 0;
    SearchProcessInfo( context->ProcessId, procName, &parentPid );

    context->ParentProcessId = parentPid;
    RtlStringCchCopyW( context->ProcName, 260, procName );

    context->CreateOptions = Data->Iopb->Parameters.Create.Options & 0x00FFFFFF;
    context->IsPost = FALSE;
    // 파일 이름 가져오기
    PFLT_FILE_NAME_INFORMATION nameInfo;
    status = FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo );
    if ( NT_SUCCESS( status ) ) {
        FltParseFileNameInformation( nameInfo );
        ExtractFileName( &nameInfo->Name, context->FileName, 260 );

        FltReleaseFileNameInformation( nameInfo );
    }
    else
    {
        RtlStringCchCopyW( context->FileName, 260, L"<UnknownFile>" );
    }

    if ( wcscmp( context->ProcName, L"<Unknown>" ) == 0 ) {
        ExFreePoolWithTag( context, 'ctxt' );
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "[IRP] IRP_MJ_CREATE( Pre ) PID=%lu, PPID=%lu, Name=%ws, FileName: %ws\n", context->ProcessId, context->ParentProcessId, context->ProcName, context->FileName );

    GENERIC_MESSAGE msg = {};
    msg.Type = MessageTypeIrpCreate;
    msg.IrpInfo = *context;
    FltSendMessage( gFilterHandle, &gClientPort, &msg, sizeof( GENERIC_MESSAGE ), NULL, NULL, &TimeOut );
    *CompletionContext = context;
    SetFileContextFromCreate( Data, FltObjects, context->FileName, context->ProcName, context->ProcessId, context->ParentProcessId );

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

// IRP_MJ_CREATE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags )
{
    TimeOut.QuadPart = -10 * 1000 * 1000;
    if ( gClientPort == NULL || CompletionContext == NULL )
        return FLT_POSTOP_FINISHED_PROCESSING;

    PIRP_CONTEXT context = ( PIRP_CONTEXT )CompletionContext;

    context->ResultStatus = Data->IoStatus.Status;
    context->IsPost = TRUE;
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[IRP] IRP_MJ_CREATE( Post ) received from PID=%lu\n", context->ProcessId );
    GENERIC_MESSAGE msg = {};
    msg.Type = MessageTypeIrpCreate;
    msg.IrpInfo = *context;
    FltSendMessage( gFilterHandle, &gClientPort, &msg, sizeof( GENERIC_MESSAGE ), NULL, NULL, &TimeOut );
    ExFreePoolWithTag( context, 'ctxt' );

    return FLT_POSTOP_FINISHED_PROCESSING;
}

// IRP_MJ_CLEANUP PreCallback
FLT_PREOP_CALLBACK_STATUS PreCleanupCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext)
{
    UNREFERENCED_PARAMETER(Data);

    // Create 경로와 동일한 패턴 유지
    if (gClientPort == NULL)
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    PIRP_CONTEXT context = (PIRP_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(IRP_CONTEXT), 'ctxt');
    if (!context)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    RtlZeroMemory(context, sizeof(IRP_CONTEXT));

    context->ProcessId = (ULONG)FltGetRequestorProcessId(Data);
    context->IsPost = FALSE;
    context->CreateOptions = 0;

    // 1) FILE_CONTEXT에서 가져오기
    PMY_FILE_CONTEXT fctx = NULL;
    if (NT_SUCCESS(FltGetFileContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT*)&fctx)) && fctx) {
        context->ParentProcessId = fctx->ParentProcessId;
        RtlStringCchCopyW(context->ProcName, 260, fctx->ProcName);
        RtlStringCchCopyW(context->FileName, 260, fctx->FileName);
        FltReleaseContext(fctx);
    }
    else {
        // 2) 보강: SearchProcessInfo + FILE_OBJECT 이름
        WCHAR pname[260] = L"<Unknown>"; ULONG ppid = 0;
        SearchProcessInfo(context->ProcessId, pname, &ppid);
        context->ParentProcessId = ppid;
        RtlStringCchCopyW(context->ProcName, 260, pname);

        if (FltObjects->FileObject && FltObjects->FileObject->FileName.Length > 0)
            ExtractFileName(&FltObjects->FileObject->FileName, context->FileName, 260);
        else
            RtlStringCchCopyW(context->FileName, 260, L"<UnknownFile>");
    }

    *CompletionContext = context;
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

// IRP_MJ_CLEANUP PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCleanupCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    if (gClientPort == NULL || CompletionContext == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    PIRP_CONTEXT context = (PIRP_CONTEXT)CompletionContext;
    context->ResultStatus = Data->IoStatus.Status;
    context->IsPost = TRUE;

    GENERIC_MESSAGE msg = {};
    msg.Type = MessageTypeIrpCleanup;
    msg.IrpInfo = *context;

    TimeOut.QuadPart = -10 * 1000 * 1000;
    (void)FltSendMessage(gFilterHandle, &gClientPort, &msg, sizeof(msg), NULL, NULL, &TimeOut);

    ExFreePoolWithTag(context, 'ctxt');
    return FLT_POSTOP_FINISHED_PROCESSING;
}

// IRP_MJ_CLOSE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCloseCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext)
{
    UNREFERENCED_PARAMETER(Data);

    if (gClientPort == NULL)
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    PIRP_CONTEXT context = (PIRP_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(IRP_CONTEXT), 'ctxt');
    if (!context)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    RtlZeroMemory(context, sizeof(IRP_CONTEXT));

    context->ProcessId = (ULONG)FltGetRequestorProcessId(Data);
    context->IsPost = FALSE;
    context->CreateOptions = 0;

    PMY_FILE_CONTEXT fctx = NULL;
    if (NT_SUCCESS(FltGetFileContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT*)&fctx)) && fctx) {
        context->ParentProcessId = fctx->ParentProcessId;
        RtlStringCchCopyW(context->ProcName, 260, fctx->ProcName);
        RtlStringCchCopyW(context->FileName, 260, fctx->FileName);
        FltReleaseContext(fctx);
    }
    else {
        WCHAR pname[260] = L"<Unknown>"; ULONG ppid = 0;
        SearchProcessInfo(context->ProcessId, pname, &ppid);
        context->ParentProcessId = ppid;
        RtlStringCchCopyW(context->ProcName, 260, pname);

        if (FltObjects->FileObject && FltObjects->FileObject->FileName.Length > 0)
            ExtractFileName(&FltObjects->FileObject->FileName, context->FileName, 260);
        else
            RtlStringCchCopyW(context->FileName, 260, L"<UnknownFile>");
    }

    *CompletionContext = context;
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

// IRP_MJ_CLOSE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCloseCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    if (gClientPort == NULL || CompletionContext == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    PIRP_CONTEXT context = (PIRP_CONTEXT)CompletionContext;
    context->ResultStatus = Data->IoStatus.Status;
    context->IsPost = TRUE;

    GENERIC_MESSAGE msg = {};
    msg.Type = MessageTypeIrpClose;
    msg.IrpInfo = *context;

    TimeOut.QuadPart = -10 * 1000 * 1000;
    (void)FltSendMessage(gFilterHandle, &gClientPort, &msg, sizeof(msg), NULL, NULL, &TimeOut);

    ExFreePoolWithTag(context, 'ctxt');
    return FLT_POSTOP_FINISHED_PROCESSING;
}

NTSTATUS InstanceSetupCallback( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType )
{
    UNREFERENCED_PARAMETER( Flags );
    UNREFERENCED_PARAMETER( VolumeDeviceType );
    UNREFERENCED_PARAMETER( VolumeFilesystemType );

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[ + ] 인스턴스 연결됨\n" );
    return STATUS_SUCCESS;
}

NTSTATUS PortConnect( PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext, ULONG SizeOfContext, PVOID* ConnectionCookie )
{
    gClientPort = ClientPort;
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[ + ] 포트 연결됨\n" );
    return STATUS_SUCCESS;
}

// 포트 연결 해제 콜백
VOID PortDisconnect( PVOID ConnectionCookie )
{
    if ( gClientPort != NULL )
    {
        FltCloseClientPort( gFilterHandle, &gClientPort );
        gClientPort = NULL;
    }
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[ - ] 포트 연결 해제\n" );
}

// Operation Registration
CONST FLT_OPERATION_REGISTRATION Callbacks[ ] = {
    { IRP_MJ_CREATE,    0,      PreCreateCallback,      PostCreateCallback },
	{ IRP_MJ_CLEANUP,   0,      PreCleanupCallback,     PostCleanupCallback },
	{ IRP_MJ_CLOSE,     0,      PreCloseCallback,       PostCloseCallback },
    { IRP_MJ_OPERATION_END }
};

// Unload
NTSTATUS DriverUnload( FLT_FILTER_UNLOAD_FLAGS Flags )
{
    if ( gServerPort != NULL )
        FltCloseCommunicationPort( gServerPort );

    if ( gFilterHandle != NULL )
        FltUnregisterFilter( gFilterHandle );

    PsSetCreateProcessNotifyRoutineEx( ProcessNotifyEx, TRUE );
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[ - ] 드라이버 언로딩 완료\n" );
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
    if ( !NT_SUCCESS( status ) )
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltRegisterFilter 실패: 0x%X\n", status );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltRegisterFilter 성공\n" );

    UNICODE_STRING uniName = RTL_CONSTANT_STRING( COMM_PORT_NAME );
    OBJECT_ATTRIBUTES oa;
    PSECURITY_DESCRIPTOR sd = NULL;

    status = FltBuildDefaultSecurityDescriptor( &sd, FLT_PORT_ALL_ACCESS );
    if ( !NT_SUCCESS( status ) )
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

    if ( !NT_SUCCESS( status ) )
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltCreateCommunicationPort 실패: 0x%X\n", status );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltCreateCommunicationPort 성공\n" );

    status = FltStartFiltering( gFilterHandle );
    if ( !NT_SUCCESS( status ) )
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltStartFiltering 실패: 0x%X\n", status );
        FltCloseCommunicationPort( gServerPort );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    status = PsSetCreateProcessNotifyRoutineEx( ProcessNotifyEx, FALSE );
    if ( !NT_SUCCESS( status ) )
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "ProcessNotify 등록 실패: 0x%X\n", status );
        return status;
    }

    ExInitializeFastMutex( &g_ProcessListLock );
    InitializeListHead( &g_ProcessNameList );

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltStartFiltering 성공 - 드라이버 로딩 완료\n" );

	return STATUS_SUCCESS;
}