#include <windows.h>
#include <stdio.h>
#include <winternl.h>
#include <fltuser.h>
#include <locale.h>
#pragma comment(lib, "FltLib.lib")

#define COMM_PORT_NAME L"\\MFDPort"

#pragma pack( push ,1 )

typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;  // FALSE: Pre, TRUE: Post
    NTSTATUS ResultStatus;
    WCHAR FileName[ 260 ];
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

typedef struct _MESSAGE_BUFFER {
    FILTER_MESSAGE_HEADER MessageHeader;
    GENERIC_MESSAGE       MessageBody;
} MESSAGE_BUFFER, * PMESSAGE_BUFFER;
#pragma pack( pop )

//////////////////////////////////////////////////

int main() {
    _wsetlocale(LC_ALL, L"Korean");
    HANDLE hPort = NULL;
    HRESULT hr = FilterConnectCommunicationPort(
        COMM_PORT_NAME,
        0,
        NULL,
        0,
        NULL,
        &hPort
    );

    if( FAILED( hr ) ) {
        wprintf( L"[!] 포트 연결에 실패했습니다: 0x%08X\n", hr );
        return 1;
    }
    wprintf( L"[+] 포트 연결에 성공했습니다.\n", hr );

    wprintf( L"위치: C:\\Dev\\UserConsole.exe\n" );
    wprintf( L"Connect MiniFilter...\n" );

    while( true ) {
        MESSAGE_BUFFER messageBuffer = { 0 };

        hr = FilterGetMessage(
            hPort,
            &messageBuffer.MessageHeader,
            sizeof( messageBuffer ),
            NULL // Optional: Timeout
        );

        if( FAILED( hr ) ) {
            wprintf( L"[!] 드라이버로부터 메세지 받아오기 실패: 0x%08X\n", hr );
            break;
        }

        const auto IrpInfo = &messageBuffer.MessageBody.IrpInfo;
        const auto ProcpInfo = &messageBuffer.MessageBody.ProcInfo;

        switch( messageBuffer.MessageBody.Type )
        {
        case MessageTypeIrpCreate: {
            if (IrpInfo->IsPost)
            {
                wprintf( L"IRP : IRP_MJ_CREATE, Type : Post, File : %ws, Result : %.259ws\n",  IrpInfo->FileName, NT_SUCCESS( IrpInfo->ResultStatus ) ? L"Success" : L"Failure" );
            }
            else
            {
                wprintf( L"IRP : IRP_MJ_CREATE, Type : Pre, File : %.259ws\n", IrpInfo->FileName );
            }
        }break;
        case MessageTypeProcEvent: {
            if (ProcpInfo->IsCreate)
            {
                wprintf(L"IRP : Proc Created, PID : %lu, ParentPID : %lu, Name : %.259ws\n", ProcpInfo->ProcessId, ProcpInfo->ParentProcessId, ProcpInfo->ImageName);
            }
            else
            {
                wprintf(L"IRP : Proc Terminated, PID : %lu, Name : %.259ws\n", ProcpInfo->ProcessId, ProcpInfo->ImageName);
            }
        }break;
        default: {
            wprintf(L"[!] 알 수 없는 메시지 타입: %d\n", messageBuffer.MessageBody.Type);
	        }

		}
    }
    CloseHandle( hPort );
    return 0;
}
