#include <windows.h>
#include <stdio.h>

#define COMM_PORT_NAME L"\\\\.\\ProcessMonitorPort"

typedef struct _PROCESS_INFO {
    ULONG ProcessId;
    WCHAR ImageName[260];
} PROCESS_INFO, * PPROCESS_INFO;

int main() {
    HANDLE hPort = CreateFileW(COMM_PORT_NAME,
        GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, 0, NULL);

    if (hPort == INVALID_HANDLE_VALUE) {
        wprintf(L"[*] 드라이버 포트 연결 실패: %d\n", GetLastError());
        return 1;
    }

    wprintf(L"[+] 포트 연결 성공! 프로세스 생성/종료 이벤트를 수신 중...\n");
    printf("Ctrl+C로 종료할 수 있습니다.\n");

    while (true) {
        PROCESS_INFO info;
        DWORD bytesRead = 0;

        if (!ReadFile(hPort, &info, sizeof(info), &bytesRead, NULL)) {
            wprintf(L"[!] ReadFile 실패: %d\n", GetLastError());
            break;
        }

        wprintf(L"[EVENT] PID: %u, 프로세스 이름: %s\n", info.ProcessId, info.ImageName);
    }

    CloseHandle(hPort);
    return 0;
}