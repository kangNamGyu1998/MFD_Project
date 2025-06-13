#include "Driver.hpp"

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
        "[Snapshot] PID=%lu, PPID=%lu, Name=%ws, FileName: %ws\n", context->ProcessId, context->ParentProcessId, context->ProcName, context->FileName );

    GENERIC_MESSAGE msg = {};
    msg.Type = MessageTypeIrpCreate;
    msg.IrpInfo = *context;
    FltSendMessage( gFilterHandle, &gClientPort, &msg, sizeof( GENERIC_MESSAGE ), NULL, NULL, &TimeOut );
    *CompletionContext = context;
    CreateStreamHandleContext( Data, FltObjects );
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
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "[IRP] IRP_MJ_CREATE( Post ) received from PID=%lu\n", context->ProcessId );
    GENERIC_MESSAGE msg = {};
    msg.Type = MessageTypeIrpCreate;
    msg.IrpInfo = *context;
    FltSendMessage( gFilterHandle, &gClientPort, &msg, sizeof( GENERIC_MESSAGE ), NULL, NULL, &TimeOut );
    ExFreePoolWithTag( context, 'ctxt' );

    return FLT_POSTOP_FINISHED_PROCESSING;
}