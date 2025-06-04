## 구성
- `Driver/` : MFD 미니필터 드라이버 프로젝트
- `UserApp/` : 콘솔 프로그램, 드라이버와 통신
- `ProcMon.sln` : 전체 솔루션

## 기능
- 드라이버는 IRP_MJ_CREATE 콜백 생성하여 Pre 콜백에서는 처리하려는 파일 이름을 출력, Post 콜백에서는 해당 요청의 처리결과를 UserConsole로 전송하여 출력.
- (05/23 업데이트) ProcessNotifyEx로 Process 생성 시 Create 핸들러 내에서 PID와 ImageName을 수집 후 Process Terminate 시 수집한 PID와 Terminate된 프로세스의 PID를 비교하여 생성/종료된 프로세스의 PID와 ProcName 출력
- 유저모드 앱은 드라이버와 커뮤니케이션 포트로 통신하여 정보 출력
- (05/30 업데이트) ProcessNotifyEx로 Process 생성 시 임시로 저장한 Pid로 IRP_MJ_CREATE 호출할 때 요청되는 Pid와 대조하여 ProcName, ParentPid 수집 후 출력.
## 실행 방법
1. `bcdedit /set testsigning on` 후 재부팅
2. StartMFD.bat를 관리자 권한으로 실행
