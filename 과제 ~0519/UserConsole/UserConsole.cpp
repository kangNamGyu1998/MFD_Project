#include <windows.h>
#include <stdio.h>
#include <winternl.h>
#include <fltuser.h>
#pragma comment(lib, "FltLib.lib")

#define COMM_PORT_NAME L"\\MFDPort"

// 드라이버와 동일한 구조체 정의
typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;  // FALSE: Pre, TRUE: Post
    WCHAR FileName[ 260 ];
    NTSTATUS ResultStatus;  // Post에서 사용
} IRP_CREATE_INFO, * PIRP_CREATE_INFO;

typedef struct _MESSAGE_BUFFER {
    FILTER_MESSAGE_HEADER MessageHeader;
    IRP_CREATE_INFO       MessageBody;
} MESSAGE_BUFFER, * PMESSAGE_BUFFER;

int main() {

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
        wprintf( L"[!] Port Connect Failed: 0x%08X\n", hr );
        return 1;
    }
    wprintf( L"[*] Port Connect Success: 0x%08X\n", hr );

    wprintf( L"C:\\Dev\\UserConsole.exe\n" );
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
            wprintf( L"[!] FilterGetMessage Failed: 0x%08X\n", hr );
            break;
        }

        IRP_CREATE_INFO* info = &messageBuffer.MessageBody;

        if( info->IsPost ) {
            wprintf( L"IRP : IRP_MJ_CREATE, Type : Post, File : %s, Result : %s\n",
                info->FileName,
                NT_SUCCESS( info->ResultStatus ) ? L"Success" : L"Failure" );
        }
        else {
            wprintf( L"IRP : IRP_MJ_CREATE, Type : Pre, File : %s\n", info->FileName );
        }
    }

    CloseHandle( hPort );
    return 0;
}
