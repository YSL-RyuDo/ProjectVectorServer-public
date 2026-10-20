# 코드네임 바이스 · CodeName VAIS — Server

[English](README.md) · [Korean](README.ko.md) · [Japanese](README.ja.md)

팀으로 개발 중인 멀티플레이 FPS. C++ 서버와 경기 상태를 Unity의 플레이 경험에 연결합니다.

**C++ · Windows IOCP · C# · Unity** · 현재 개발 중

**2026.03 - 현재 · 10명 · 소프트웨어 5명, 그래픽 5명**

서버 개발·Unity 멀티플레이 연동

[개발 사례 보기](https://ysl-ryudo.github.io/ko/work/vector/) · [Portfolio](https://ysl-ryudo.github.io/ko/)

[ProjectVectorServer](https://github.com/YSL-RyuDo/ProjectVectorServer) (비공개 · 접근 권한 필요) · [projectVector](https://github.com/YSL-RyuDo/projectVector) (비공개 · 접근 권한 필요)

## 시스템별 담당 내용

| 영역 | 구현 |
|---|---|
| 연결·통신 | IOCP · 세션 · 패킷 디스패치 |
| 게임 서비스 | 방·경기 수명주기, 커리어·프로필·상점·소셜 데이터 |
| Unity 연동 | 게임플레이 패킷, 로비·결과, Sentinel 능력과 음성 권한 |
| 복원 경계 | 최신 지속 표시 상태 보관 · 게임 행동 재실행 제외 |

## ReconnectJournal

재접속한 플레이어에게 무엇을 다시 보여주고, 어떤 게임 행동은 반복하지 않아야 할까?

1. **복원할 상태 선택** — Observe는 명시된 지속 표시 패킷을 수집합니다. 피해·보상·플레이어 명령은 복원 기록에 포함하지 않습니다.
2. **최신 상태로 교체** — 패킷 종류와 개체 ID를 키로 저장합니다. 후속 갱신은 같은 항목을 교체하므로 행동 이력을 순서대로 재생하는 방식과 구분됩니다.
3. **수명주기 변경 반영** — 새 경기·라운드 패킷은 기록을 초기화합니다. 제거 패킷은 대응하는 개체 상태를 지우고, 서로 배타적인 상태는 이전 항목을 정리합니다.
4. **남은 시간으로 복원** — Snapshot은 저장된 지속 시간에서 steady clock 경과 시간을 빼고 0 이상으로 제한합니다. 0초 상태 패킷도 전달해 클라이언트의 오래된 상태를 해제합니다.

정식 명칭은 CodeName VAIS이며, Project Vector는 이전 임시 이름입니다. 저장소 이름은 유지합니다.

## 개발 시연 영상

- [Lobby UI](https://www.youtube.com/watch?v=WqRBn-yWVdo)
- [Store system](https://www.youtube.com/watch?v=OBFWkZE2rzo)
- [Profile system](https://www.youtube.com/watch?v=A3CkQvnQEGI)
- [Earlier Project Vector demo](https://www.youtube.com/watch?v=oga4ai_1Ugg)

## 내 기여

- 게임 클라이언트를 위한 IOCP 연결 처리, 세션과 패킷 디스패치를 구현했습니다.
- 방·경기 수명주기, 프로필·커리어 데이터, 소셜 기능, 상점 서비스와 클라이언트 규격을 작업했습니다.
- 재접속 시 표시 상태 복원과 파티·팀 음성 권한 처리를 추가했습니다. ReconnectJournal은 피해·보상·플레이어 명령을 재실행하지 않고 지속 상태를 보관합니다.

## 기여 근거

- [IOCP](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/IocpCore.cpp)
- [ReconnectJournal](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h)
- [Validation sources](https://github.com/YSL-RyuDo/ProjectVectorServer/tree/3066fe6d90fb4a56a6c440af56c4e119188e73fc/Tests)

커밋은 설명한 변경의 근거이며 기여율 점수가 아닙니다. 외부 에셋과 팀원 작업의 크레딧은 구분합니다.

## 실행·코드 확인

Windows C++ 워크로드가 설치된 Visual Studio에서 [ProjectVectorServer/ProjectVectorServer.sln](ProjectVectorServer/ProjectVectorServer.sln)을 여세요. 구성·데이터 경로·의존성·접속 설정을 확인한 뒤 대응하는 클라이언트를 실행하세요. 기존 계정 데이터를 보존하세요.

[projectVector](https://github.com/YSL-RyuDo/projectVector)

## 개발·검증 범위

현재 개발 중입니다. 재접속·경기 수명주기·상점·소셜·음성 규격 테스트 소스가 있지만, 소스 존재 자체가 현재 버전의 통과 기록은 아닙니다. 음성 검증 기록과 남은 두 PC 검사는 Docs/VoiceChatSetup.md를 확인하세요. 현재 TCP에는 TLS가 없으며 문서의 개발 환경은 신뢰 LAN/VPN입니다. IOCP 사용만으로 측정된 확장성을 주장하지 않습니다.

## 상세 문서

- [Docs/VoiceChatSetup.md](Docs/VoiceChatSetup.md)
- [Docs/CombatReportProtocol.md](Docs/CombatReportProtocol.md)
