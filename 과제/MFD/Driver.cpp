#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include <ntstrsafe.h>

// 필터 핸들, 커뮤니케이션 포트 선언
PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

// 사용자와 통신할 포트 이름 정의
#define COMM_PORT_NAME L"\\FilterPort\\ProcessMonitorPort"

// 유저 콘솔에 보낼 프로세스 정보 구조체
typedef struct _PROCESS_INFO {
    ULONG ProcessId;
    WCHAR ImageName[260];
} PROCESS_INFO, * PPROCESS_INFO;

// 포트 연결 콜백 - 유저 모드 애플리케이션과 연결될 때 호출됨
NTSTATUS PortConnect(PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext,
    ULONG SizeOfContext, PVOID* ConnectionCookie)
{
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);
    UNREFERENCED_PARAMETER(ConnectionCookie);

    gClientPort = ClientPort;
    DbgPrint("포트 연결됨\n");
    return STATUS_SUCCESS;
}

// 포트 연결 해제 콜백 - 유저 모드 애플리케이션이 연결을 끊을 때 호출됨
VOID PortDisconnect(PVOID ConnectionCookie) {
    UNREFERENCED_PARAMETER(ConnectionCookie);

    if (gClientPort) {
        FltCloseClientPort(gFilterHandle, &gClientPort);
        DbgPrint("포트 연결 해제됨\n");
    }
}

// 프로세스 생성 또는 종료 시 호출되는 콜백
// 유저 콘솔에 메시지를 전송함
VOID ProcessNotifyRoutineEx(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    UNREFERENCED_PARAMETER(Process);

    if (!gClientPort) return;

    PROCESS_INFO info = { 0 };
    info.ProcessId = (ULONG)(ULONG_PTR)ProcessId;

    if (CreateInfo) {
        if (CreateInfo->ImageFileName) {
            RtlStringCchCopyW(info.ImageName, 260, CreateInfo->ImageFileName->Buffer);
        }
        else {
            RtlStringCchCopyW(info.ImageName, 260, L"<Unknown>");
        }
    }
    else {
        RtlStringCchCopyW(info.ImageName, 260, L"<Terminated>");
    }

    // 유저 콘솔로 메시지 전송
    FltSendMessage(gFilterHandle, &gClientPort, &info, sizeof(info), NULL, NULL, NULL);
}

// 드라이버가 언로드될 때 호출됨
// 등록된 콜백과 포트를 정리함
NTSTATUS DriverUnload(FLT_FILTER_UNLOAD_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    PsSetCreateProcessNotifyRoutineEx(ProcessNotifyRoutineEx, TRUE);

    if (gServerPort) {
        FltCloseCommunicationPort(gServerPort);
    }

    if (gFilterHandle) {
        FltUnregisterFilter(gFilterHandle);
    }

    DbgPrint("드라이버 언로드 완료\n");
    return STATUS_SUCCESS;
}

// 필터가 사용할 작업 정의 (없더라도 IRP_MJ_OPERATION_END는 반드시 필요함)
CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_OPERATION_END }
};

// 드라이버 진입점 - 드라이버가 로드될 때 가장 먼저 실행됨
extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;

    // 필터 등록 정보 구조체 초기화
    FLT_REGISTRATION filterRegistration = {
        sizeof(FLT_REGISTRATION),        // 구조체 크기
        FLT_REGISTRATION_VERSION,        // 버전
        0,                               // Flags
        NULL,                            // Context
        Callbacks,                       // Operation callbacks
        DriverUnload,                    // Unload callback
        NULL, NULL, NULL, NULL,          // Instance callbacks
        NULL, NULL, NULL, NULL           // 기타 콜백
    };

    UNICODE_STRING uniName = RTL_CONSTANT_STRING(COMM_PORT_NAME);
    OBJECT_ATTRIBUTES oa;

    InitializeObjectAttributes(&oa, &uniName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    // 필터 등록
    status = FltRegisterFilter(DriverObject, &filterRegistration, &gFilterHandle);
    if (!NT_SUCCESS(status)) {
        DbgPrint("FltRegisterFilter 실패: 0x%08X\n", status);
        return status;
    }

    // 커뮤니케이션 포트 생성
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

    if (!NT_SUCCESS(status)) {
        DbgPrint("FltCreateCommunicationPort 실패: 0x%08X\n", status);
        FltUnregisterFilter(gFilterHandle);
        return status;
    }


    // 프로세스 생성 알림 콜백 등록
    status = PsSetCreateProcessNotifyRoutineEx(ProcessNotifyRoutineEx, FALSE);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PsSetCreateProcessNotifyRoutineEx 실패: 0x%08X\n", status);
        FltCloseCommunicationPort(gServerPort);
        FltUnregisterFilter(gFilterHandle);
        return status;
    }

    // 필터링 시작
    status = FltStartFiltering(gFilterHandle);
    if (!NT_SUCCESS(status)) {
        DbgPrint("FltStartFiltering 실패: 0x%08X\n", status);
        PsSetCreateProcessNotifyRoutineEx(ProcessNotifyRoutineEx, TRUE);
        FltCloseCommunicationPort(gServerPort);
        FltUnregisterFilter(gFilterHandle);
        return status;
    }

    DbgPrint("드라이버 로딩 성공\n");
    return STATUS_SUCCESS;
}