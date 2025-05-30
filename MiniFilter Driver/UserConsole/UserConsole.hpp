#pragma once

#include <windows.h>
#include <stdio.h>
#include <winternl.h>
#include <fltuser.h>
#include <locale.h>
#pragma comment( lib, "FltLib.lib" )
#pragma comment( lib, "ntdll.lib" )

#define COMM_PORT_NAME L"\\MFDPort"
#pragma pack( push ,1 )
typedef struct _PROC_EVENT_INFO {
    BOOLEAN IsCreate;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ImageName[260];
} PROC_EVENT_INFO, * PPROC_EVENT_INFO;

typedef enum _MESSAGE_TYPE {
    MessageTypeIrpCreate,
    MessageTypeProcEvent
} MESSAGE_TYPE;

typedef struct _IRP_CONTEXT {
    BOOLEAN IsPost;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ProcName[260];
    WCHAR FileName[260];
    ULONG CreateOptions;
    NTSTATUS ResultStatus;
} IRP_CONTEXT, * PIRP_CONTEXT;

typedef struct _GENERIC_MESSAGE {
    MESSAGE_TYPE Type;
    union {
        IRP_CONTEXT IrpInfo;
        PROC_EVENT_INFO ProcInfo;
    };
} GENERIC_MESSAGE, * PGENERIC_MESSAGE;

typedef struct _MESSAGE_BUFFER {
    FILTER_MESSAGE_HEADER MessageHeader;
    GENERIC_MESSAGE       MessageBody;
} MESSAGE_BUFFER, * PMESSAGE_BUFFER;
#pragma pack( pop )

struct { ULONG Flag; const wchar_t* Name; } flags[] = {
        { 0x00000001, L"FILE_DIRECTORY_FILE" },
        { 0x00000002, L"FILE_WRITE_THROUGH" },
        { 0x00000004, L"FILE_SEQUENTIAL_ONLY" },
        { 0x00000008, L"FILE_NO_INTERMEDIATE_BUFFERING" },
        { 0x00000010, L"FILE_SYNCHRONOUS_IO_ALERT" },
        { 0x00000020, L"FILE_SYNCHRONOUS_IO_NONALERT" },
        { 0x00000040, L"FILE_NON_DIRECTORY_FILE" },
        { 0x00000080, L"FILE_CREATE_TREE_CONNECTION" },
        { 0x00000100, L"FILE_COMPLETE_IF_OPLOCKED" },
        { 0x00000200, L"FILE_NO_EA_KNOWLEDGE" },
        { 0x00000400, L"FILE_OPEN_REMOTE_INSTANCE" },
        { 0x00000800, L"FILE_RANDOM_ACCESS" },
        { 0x00001000, L"FILE_DELETE_ON_CLOSE" },
        { 0x00002000, L"FILE_OPEN_BY_FILE_ID" },
        { 0x00004000, L"FILE_OPEN_FOR_BACKUP_INTENT" },
        { 0x00008000, L"FILE_NO_COMPRESSION" },
        { 0x00010000, L"FILE_OPEN_REQUIRING_OPLOCK" },
        { 0x00020000, L"FILE_DISALLOW_EXCLUSIVE" },
        { 0x00040000, L"FILE_SESSION_AWARE" },
        { 0x00080000, L"FILE_RESERVE_OPFILTER" }
};
