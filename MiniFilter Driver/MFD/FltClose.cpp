#include "Driver.hpp"

// IRP_MJ_CLOSE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCloseCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext )
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

// IRP_MJ_CLOSE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCloseCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags )
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( CompletionContext );
    UNREFERENCED_PARAMETER( Flags );

    TimeOut.QuadPart = -10 * 1000 * 1000;
    if ( gClientPort == NULL )
        return FLT_POSTOP_FINISHED_PROCESSING;

    PMY_STREAMHANDLE_CONTEXT ctx = NULL;
    if ( NT_SUCCESS( FltGetStreamHandleContext( FltObjects->Instance, FltObjects->FileObject, ( PFLT_CONTEXT* )&ctx ) ) && ctx )
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
            "[IRP] IRP_MJ_CLOSE( Post ) from PID=%lu, PPID=%lu, Name=%ws, File=%ws\n",
            ctx->ProcessId, ctx->ParentProcessId, ctx->ProcName, ctx->FileName );

        // 메시지 전송 예시 ( 옵션 )
        GENERIC_MESSAGE msg = {};
        msg.Type = MessageTypeIrpClose;
        RtlZeroMemory( &msg.IrpInfo, sizeof( IRP_CONTEXT ) );

        msg.IrpInfo.IsPost = TRUE;
        msg.IrpInfo.ProcessId = ctx->ProcessId;
        msg.IrpInfo.ParentProcessId = ctx->ParentProcessId;
        RtlStringCchCopyW( msg.IrpInfo.ProcName, 260, ctx->ProcName );
        RtlStringCchCopyW( msg.IrpInfo.FileName, 260, ctx->FileName );
        msg.IrpInfo.ResultStatus = STATUS_SUCCESS;

        FltSendMessage( gFilterHandle, &gClientPort, &msg, sizeof( msg ), NULL, NULL, &TimeOut );
        FltReleaseContext( ctx );
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}
