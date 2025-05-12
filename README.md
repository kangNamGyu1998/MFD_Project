## 구성
- `Driver/` : MFD 미니필터 드라이버 프로젝트
- `UserApp/` : 콘솔 프로그램, 드라이버와 통신
- `ProcMon.sln` : 전체 솔루션

## 기능
- 드라이버는 `CreateProcessNotifyRoutineEx`를 등록하여 프로세스 생성/종료 감지
- 유저모드 앱은 드라이버와 커뮤니케이션 포트로 통신하여 정보 출력

## 실행 방법
1. `bcdedit /set testsigning on` 후 재부팅
2. 드라이버 빌드 후 INF 설치:
