#include <windows.h>
#include <stdio.h>
#include <winternl.h>
<<<<<<< HEAD

#define COMM_PORT_NAME L"\\FilterPort\\ProcessMonitorPort"
=======
#define COMM_PORT_NAME L"\\.\ProcessMonitorPort"
>>>>>>> 0061ebd59bed839b77c237bc5c567ba79200e588

// 드라이버와 동일한 구조체 정의
typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;  // FALSE: Pre, TRUE: Post
    WCHAR FileName[ 260 ];
    NTSTATUS ResultStatus;  // Post에서 사용
} IRP_CREATE_INFO, * PIRP_CREATE_INFO;

int main() {
    HANDLE hPort = CreateFileW( COMM_PORT_NAME,
        GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, 0, NULL );

    if( hPort == INVALID_HANDLE_VALUE ) {
        wprintf( L"[*] 드라이버 포트 연결 실패: %d\n", GetLastError() );
        return 1;
    }

    wprintf( L"%s\n", L"C:\\Dev\\MiniM.exe" );
    wprintf( L"Connect MiniFilter...\n" );

    while( true ) {
        IRP_CREATE_INFO info;
        DWORD bytesRead = 0;

        if( !ReadFile( hPort, &info, sizeof( info ), &bytesRead, NULL ) ) {
            wprintf( L"[!] ReadFile 실패: %d\n", GetLastError() );
            break;
        }

        if( bytesRead == sizeof( IRP_CREATE_INFO ) ) {
            if( info.IsPost ) {
                wprintf( L"IRP : IRP_MJ_CREATE, Type : Post, File : %s, Result : %s\n",
                    info.FileName,
                    NT_SUCCESS( info.ResultStatus ) ? L"Success" : L"Failure" );
            }
            else {
                wprintf( L"IRP : IRP_MJ_CREATE, Type : Pre, File : %s\n", info.FileName );
            }
        }
    }

    CloseHandle( hPort );
    return 0;
}
