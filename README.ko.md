# 코드네임 바이스, CodeName VAIS / Server

<!-- portfolio-languages -->
[![English](https://ysl-ryudo.github.io/assets/diagrams/language-en.svg)](README.md) [![한국어 (현재 언어)](https://ysl-ryudo.github.io/assets/diagrams/language-ko-current.svg)](README.ko.md) [![日本語](https://ysl-ryudo.github.io/assets/diagrams/language-ja.svg)](README.ja.md)
<!-- /portfolio-languages -->

팀으로 개발 중인 멀티플레이 FPS. C++ 서버와 경기 상태를 Unity의 플레이 경험에 연결합니다.

| 프로젝트 정보 | 내용 |
|---|---|
| 개발 상태 | 현재 개발 중 |
| 사용 기술 | C++, Windows IOCP, C#, Unity |
| 기간과 팀 구성 | 2026.03 ~ 현재, 10명, 소프트웨어 5명, 그래픽 5명 |
| 내 역할 | C++ 서버, Unity 게임 진행과 UI, 제작 도구 |

[개발 사례 보기](https://ysl-ryudo.github.io/ko/work/vector/), [Portfolio](https://ysl-ryudo.github.io/ko/)

[![코드네임 바이스, CodeName VAIS](https://i.ytimg.com/vi/WqRBn-yWVdo/hqdefault.jpg)](https://www.youtube.com/watch?v=WqRBn-yWVdo)

[Lobby UI](https://www.youtube.com/watch?v=WqRBn-yWVdo), [Store system](https://www.youtube.com/watch?v=OBFWkZE2rzo), [Profile system](https://www.youtube.com/watch?v=A3CkQvnQEGI), [Earlier Project Vector demo](https://www.youtube.com/watch?v=oga4ai_1Ugg)

## 게임에서 보는 구현

[![이동 표적 훈련에서 실제 게임의 발사와 반동, 탄약 처리를 사용합니다.](https://ysl-ryudo.github.io/assets/gameplay/vais-practice.webp)](https://ysl-ryudo.github.io/assets/gameplay/vais-practice.webp)

이동 표적 훈련에서 실제 게임의 발사와 반동, 탄약 처리를 사용합니다.

[![캐릭터와 무기, 표적 설정이 훈련 내용과 기록 조건을 결정합니다.](https://ysl-ryudo.github.io/assets/gameplay/vais-loadout.webp)](https://ysl-ryudo.github.io/assets/gameplay/vais-loadout.webp)

캐릭터와 무기, 표적 설정이 훈련 내용과 기록 조건을 결정합니다.

[플레이 영상 보기](https://ysl-ryudo.github.io/ko/work/vector/#gameplay), CodeName VAIS 사격 훈련장, YSL 브랜치, 53초. 2026년 10월 20일 Unity에서 자동 입력으로 녹화했습니다.

## 게임 전체에서 맡은 구현

C++ 서버 서비스와 Unity 경기 진행, 프로필과 소셜 기능, 보상 연출과 에디터 도구를 구현했습니다. 캐릭터 능력과 전투 상태를 네트워크 프로토콜에 연결했습니다.

[![담당 구현 흐름](https://ysl-ryudo.github.io/assets/diagrams/vector-flow-ko.svg)](https://ysl-ryudo.github.io/assets/diagrams/vector-flow-ko.svg)

### 방, 팀 구성과 경기 진입

서버에서 방 생성과 입퇴장, 슬롯 변경과 교환 응답, 방장 이전, 강퇴를 구현했습니다. 캐릭터 확정과 로딩 완료를 경기 시작 전의 별도 단계로 다루고, PickManager로 선택 입력과 수신 상태를 Unity 화면에 연결했습니다.

[RoomManager.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/RoomManager.cpp), [내 변경 20f7bde](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/a2ab47ba2f3eff317df5ce0e2a056e7dd0926f17), [PickManager.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Pick/PickManager.cs), [내 변경 8fcd6f1](https://github.com/YSL-RyuDo/projectVector/commit/8fcd6f1e91fbcd627a852de66db88b444884ccea)

### 전투와 목표 수행 프로토콜

발사, 재장전, 무기 교체, 피해와 사망, 스파이크 획득과 투하, 설치와 해체의 서버 처리를 구현했습니다. 플레이어 행동을 방과 세션 상태에 연결하는 작업입니다.

[PacketDispatcher.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/PacketDispatcher.cpp), [내 변경 3066fe6](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/f3c4884a4270746434464630226e662fbe5fd1e6), [Session.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/Session.cpp), [내 변경 3066fe6](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/f3c4884a4270746434464630226e662fbe5fd1e6)

### 캐릭터 능력의 네트워크 연동

Sentinel 스킬을 확장해 장치와 영역 상태를 동기화하고 C 스킬 전용 네트워크 매니저를 구현했습니다. 서버에서는 다른 캐릭터 계열의 능력 요청과 이벤트도 처리했습니다.

[SentinelCNetworkManager.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Skill/SentinelCNetworkManager.cs), [내 변경 756c9a0](https://github.com/YSL-RyuDo/projectVector/commit/756c9a09f32ed4a073d5d3d60008b1aaa44f0f60), [SentinelSkill_E.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/Character/Sentinel/SentinelSkill_E.cs), [내 변경 5e5c3c5](https://github.com/YSL-RyuDo/projectVector/commit/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350)

### 캐릭터 선택과 로딩 연출

경기 진입 흐름에 맞춰 캐릭터 선택 연출, 맵 소개와 로딩 연출을 구현했습니다. 선택한 캐릭터와 팀 슬롯, 전환 화면을 네트워크 준비 단계에 맞춰 유지하는 클라이언트 작업도 담당했습니다.

[PickSelectionPresentation.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Pick/PickSelectionPresentation.cs), [내 변경 76d985b](https://github.com/YSL-RyuDo/projectVector/commit/76d985b6bd9dff1fb7b4f08002e64f98d9bb8bc3), [MatchLoadingPresentation.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Pick/MatchLoadingPresentation.cs), [내 변경 8fcd6f1](https://github.com/YSL-RyuDo/projectVector/commit/8fcd6f1e91fbcd627a852de66db88b444884ccea)

### 커리어와 성장 화면

커리어 표시와 프로필의 전적 데이터를 구현하고 서버의 경기 기록, 순위와 성장 상태에 연결했습니다. 레벨 보상은 화면에서 해금만 표시하는 데 그치지 않고 수령 조건과 저장 상태를 클라이언트 보상 화면에 연결했습니다.

[CareerPanelController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/CareerPanelController.cs), [내 변경 76d985b](https://github.com/YSL-RyuDo/projectVector/commit/76d985b6bd9dff1fb7b4f08002e64f98d9bb8bc3), [MyPageLevelRewardController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/MyPageLevelRewardController.cs), [내 변경 955101d](https://github.com/YSL-RyuDo/projectVector/commit/955101d782e975c1812ed94de893e4b263a266e3), [CareerManager.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/CareerManager.cpp), [내 변경 39de6fd](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/b6d1fe1f5c794f027e93025a8d03e748365d5f59)

### 프로필 편집과 ChipCraft

표시할 전적의 드래그 슬롯과 ChipCraft 메달 배치를 포함한 프로필 편집을 구현했습니다. 서버에서는 위치, 회전, 크기와 소유 여부를 다루는 데이터로 배치를 관리하고, Unity에서는 직접 옮기고 확인하는 편집 화면을 구현했습니다.

[MyPageChipCraftController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/MyPageChipCraftController.cs), [내 변경 06f82cc](https://github.com/YSL-RyuDo/projectVector/commit/06f82cc44d00c33af781710f0f2ace3d10353969), [MyPageStatDropSlot.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/MyPageStatDropSlot.cs), [내 변경 632e516](https://github.com/YSL-RyuDo/projectVector/commit/632e516b428b768b7eabed586afc89bfcd154af3), [ProfileManager.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/ProfileManager.cpp), [내 변경 f65f26d](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/edc2517f7c4cfdfee0594a869e5bdacdd544cccb)

### 친구와 플레이어 관계

친구 패널과 목록 모델, 서버의 요청과 수락, 취소, 차단, 메모와 접속 상태 처리를 구현했습니다. 양쪽이 공유하는 친구 관계와 한쪽에서 시작하는 요청이나 차단을 다른 데이터 관계로 다뤘습니다.

[FriendPanelController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/FriendPanelController.cs), [내 변경 06f82cc](https://github.com/YSL-RyuDo/projectVector/commit/06f82cc44d00c33af781710f0f2ace3d10353969), [FriendManager.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/FriendManager.cpp), [내 변경 675d37f](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/07860d73d4ee93b800e32de05300832d8c0a6cc8)

### 명예와 호감도

명예 화면과 보상 수령에 필요한 활동 기록, 호감도 상태와 대화 완료 처리를 구현했습니다. 보상 표시를 서버의 조건과 저장된 성장 상태에 연결해 화면만으로 소유 여부를 결정하지 않도록 했습니다.

[MyPageHonorController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/MyPageHonorController.cs), [내 변경 f4457e2](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43), [HonorActivity.h (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/HonorActivity.h), [내 변경 39de6fd](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/b6d1fe1f5c794f027e93025a8d03e748365d5f59), [AffinityService.h (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/AffinityService.h), [내 변경 39de6fd](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/b6d1fe1f5c794f027e93025a8d03e748365d5f59)

### 구매 처리와 보상 연출

거래 재요청 처리와 Unity 뽑기 연출, 건너뛰기와 자원 정리를 구현했습니다. 아래 설계 설명에서는 확정된 보상과 중단 가능한 연출의 경계를 실패와 중복 요청까지 포함해 설명합니다.

[ShopService.h (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/ShopService.h), [내 변경 39de6fd](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/b6d1fe1f5c794f027e93025a8d03e748365d5f59), [GachaManager.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/Shop/GachaManager.cs), [내 변경 8563836](https://github.com/YSL-RyuDo/projectVector/commit/856383681df4b817b62db98cbf8c4f01b98ba270)

### 월드 데이터 추출과 빌드 도구

서버용 충돌체, 지면과 높이 영역 데이터를 추출하는 Unity 에디터 도구와 빌드 도구를 구현했습니다. 씬에서 제작한 정보를 런타임 데이터에 연결하고, 형상 수치를 매번 손으로 옮기는 작업을 줄이는 제작 과정도 담당했습니다.

[ServerColliderExporter.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Editor/ServerColliderExporter.cs), [내 변경 70d856f](https://github.com/YSL-RyuDo/projectVector/commit/70d856fcd5b1502db05f90d25c5dacdb90315d76), [ServerGroundExporter.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Editor/ServerGroundExporter.cs), [내 변경 70d856f](https://github.com/YSL-RyuDo/projectVector/commit/70d856fcd5b1502db05f90d25c5dacdb90315d76), [SafePlayerBuild.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Editor/SafePlayerBuild.cs), [내 변경 632e516](https://github.com/YSL-RyuDo/projectVector/commit/632e516b428b768b7eabed586afc89bfcd154af3)

### 사격 훈련장과 조건별 기록

캐릭터와 무기 선택, 다섯 가지 훈련 모드, 거리와 방어구 설정, 사격 통계를 구현했습니다. 발사와 반동, 탄약과 재장전은 게임에서 사용하는 무기 경로를 공유합니다. 무기와 캐릭터, 난이도와 표적 조건별로 기록을 나누고, 진행 중 장비를 바꾼 플레이는 최고 기록에서 제외합니다.

[PracticeRangeController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeRangeController.cs), [내 변경 f4457e2](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43), [PracticeLoadout.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeLoadout.cs), [내 변경 f4457e2](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43), [PracticeWeaponInput.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeWeaponInput.cs), [내 변경 f4457e2](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43)

## 기여 근거

[IOCP](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/f3c4884a4270746434464630226e662fbe5fd1e6/ProjectVectorServer/ProjectVectorServer/IocpCore.cpp)

[ReconnectJournal](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/f3c4884a4270746434464630226e662fbe5fd1e6/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h)

[Validation sources](https://github.com/YSL-RyuDo/ProjectVectorServer-public/tree/f3c4884a4270746434464630226e662fbe5fd1e6/Tests)

## 구현과 설계

멀티플레이 FPS에서는 통신뿐 아니라 경기 수명주기와 플레이어에게 보이는 상태가 연결되어야 합니다. 재접속에서는 복구할 상태와 다시 실행하면 안 되는 행동을 구분해야 합니다.

IOCP 서버의 세션, 패킷 처리부터 Unity의 경기, 프로필, 상점 연동, 재접속 시 표시 상태 복원까지 작업했습니다. ReconnectJournal에는 지속 상태를 보관하고 피해, 보상, 플레이어 명령 재실행은 제외했습니다.

### ReconnectJournal

**복원할 상태 선택**

Observe는 명시된 지속 표시 패킷을 수집합니다. 피해, 보상, 플레이어 명령은 복원 기록에 포함하지 않습니다.

**최신 상태로 교체**

패킷 종류와 개체 ID를 키로 저장합니다. 후속 갱신은 같은 항목을 교체하므로 행동 이력을 순서대로 재생하는 방식과 구분됩니다.

**수명주기 변경 반영**

새 경기, 라운드 패킷은 기록을 초기화합니다. 제거 패킷은 대응하는 개체 상태를 지우고, 서로 배타적인 상태는 이전 항목을 정리합니다.

**남은 시간으로 복원**

Snapshot은 저장된 지속 시간에서 steady clock 경과 시간을 빼고 0 이상으로 제한합니다. 0초 상태 패킷도 전달해 클라이언트의 오래된 상태를 해제합니다.

정식 명칭은 CodeName VAIS이며, Project Vector는 이전 임시 이름입니다. 저장소 이름은 유지합니다.

## 코드의 동작과 구현 방식을 택한 이유

[![구매 확정과 보상 연출](https://ysl-ryudo.github.io/assets/diagrams/vector-design-ko.svg)](https://ysl-ryudo.github.io/assets/diagrams/vector-design-ko.svg)

같은 요청은 저장된 결과를 재사용하고, 저장 실패 시 거래를 되돌립니다. 건너뛰기와 닫기는 보상 확정 뒤의 연출을 제어합니다.

### 구매 응답을 받지 못해도 다시 결제되지 않도록

**구현 흐름**

상점 요청에 요청 ID를 넣고, 서버는 재화를 차감하기 전에 저장된 거래 기록부터 확인합니다. 새 거래라면 보상을 계산하고 CareerManager를 통해 재화, 소유권, 거래 기록을 함께 저장합니다. 같은 요청이 다시 오면 기록된 보상을 반환하고, 저장에 실패하면 메모리의 지갑 상태를 복구한 뒤 실패를 응답합니다.

**이 구조가 필요한 이유**

구매 버튼을 비활성화하는 것만으로는 응답 유실에 대응할 수 없습니다. 클라이언트가 보기에는 요청이 처리되지 않은 경우와 결제는 끝났지만 응답만 잃은 경우가 같기 때문입니다. 요청 ID와 결과를 함께 보관하면 재접속이나 재시작 후에도 두 경우를 구분할 수 있습니다. 상점이 기존 지갑을 함께 사용하므로 경기 보상 잔액과 상점 잔액을 따로 맞추는 문제도 줄어듭니다.

**대안과 현재 구현의 절충**

버튼 잠금은 간단하지만 해당 화면 안에서만 유효합니다. 메모리에 요청을 기억하는 방법도 있지만 재시작하면 기록을 잃습니다. 현재처럼 요청과 결과를 영속 저장하면 재시작까지 다루는 대신 원장을 관리해야 합니다. DB 트랜잭션과 고유 요청 키를 쓰는 대안도 있으며, 이 경우 DB 운영과 데이터 이관 비용이 추가됩니다.

**장단점과 검증 범위**

같은 잠금을 사용하는 작업은 저장이 끝날 때까지 기다리므로 저장 지연이 다른 요청에도 영향을 줍니다. 화면은 최근 20건을 표시하지만 중복 차감 방지용 원장은 이전 기록도 보관해 계속 커집니다. SharedShopWalletTests에는 재시작 후 재요청, 동시 중복 요청과 저장 실패 시 복구를 검사하는 코드가 있습니다.

[ProjectVectorServer/ShopService.h](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/ShopService.h), [ProjectVectorServer/CareerManager.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/CareerManager.cpp), [ProjectVectorServer/SharedShopWalletTests.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/Tests/SharedShopWalletTests.cpp), [projectVector/ShopGachaImplementation.md](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Docs/ShopGachaImplementation.md)

### 뽑기 결과와 연출의 수명을 분리한 이유

**구현 흐름**

Unity 클라이언트는 서버 결과를 받은 다음 GachaManager의 연출을 시작합니다. 코루틴은 준비, 이동, 타격, 실체화, 결과 표시 단계를 unscaled 시간으로 진행합니다. 건너뛰기는 코루틴을 멈추고 임시 오브젝트, RenderTexture, Material을 정리한 다음 이미 확정된 결과를 보여줍니다. UI는 씬에 저장한 참조를 사용하고, 3D 미리보기용 임시 오브젝트는 별도로 생성합니다.

**이 구조가 필요한 이유**

연출은 중단할 수 있지만 중단할 때마다 구매를 다시 추첨해서는 안 됩니다. 두 수명을 분리하면 건너뛰기나 닫기가 보상 지급과 취소를 일으키지 않습니다. unscaled 시간은 게임 시간이 멈춘 상태에서도 연출을 진행하기 위한 선택입니다. UI를 씬에 저장해 두면 실행 중 생성 코드를 거치지 않고도 배치와 연결을 편집할 수 있습니다.

**대안과 현재 구현의 절충**

코루틴은 시간 흐름과 정리를 코드에서 함께 추적하기 쉽습니다. Timeline은 제작자가 시퀀스를 시각적으로 편집하기 좋고, 트윈은 개별 움직임을 짧게 표현하기 좋습니다. 현재 코루틴은 코드 수준 제어를 얻는 대신 시각 편집 지원이 적습니다. 어느 방법이든 취소 처리는 필요하고 같은 서버 확정 보상을 표시해야 합니다.

**장단점과 검증 범위**

코루틴으로 단계와 흐름을 한곳에서 추적할 수 있는 대신, 단계 순서와 취소 시 정리를 직접 관리해야 합니다. Timeline이나 트윈 기반으로 바꾸면 연출 편집은 달라질 수 있지만, 이동과 건너뛰기가 구매를 재실행하지 않는 경계는 그대로 필요합니다. Unity의 가비지 컬렉션만으로 이러한 렌더링 자원 정리를 대신할 수도 없습니다.

[projectVector/GachaManager.cs](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/Shop/GachaManager.cs), [projectVector/ShopGachaImplementation.md](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Docs/ShopGachaImplementation.md)

### 소켓 연결만으로 재접속을 끝내지 않는 이유

**구현 흐름**

서버는 인증된 경기 중 세션을 복구 시간 동안 보관하고 새 통신 연결을 기존 논리 세션에 붙입니다. 세대 번호로 이전 연결의 지연 정리 작업이 복구된 세션에 적용되지 않도록 구분합니다. Unity의 NetworkReconnect는 씬 로딩과 플레이어 오브젝트 준비를 기다리고 단계별 복원 확인을 거쳐 일반 입력을 재개합니다. ReconnectJournal은 피해와 보상을 다시 실행하는 대신 지속되는 표시 상태의 최신 값을 보관합니다.

**이 구조가 필요한 이유**

통신 연결과 경기 참가자의 수명은 다릅니다. 연결 때마다 새 참가자를 만들면 기존 경기 상태를 잃고, 오브젝트가 생기기 전에 패킷부터 적용하면 일부 상태만 복원될 수 있습니다. 단계를 나누면 씬, 객체, 위치, 경기 상태 사이의 준비 순서가 명시됩니다. 최신 상태를 보관하는 방식은 과거 행동을 새 명령으로 잘못 실행하는 문제도 피합니다.

**대안과 현재 구현의 절충**

새 세션으로 시작하면 단순하지만 진행 중인 경기를 이어 주지 못합니다. 모든 이벤트를 재생하는 방식은 재실행 규칙을 엄밀하게 관리해야 합니다. 현재 구조는 논리 세션을 보관하고 필요한 상태를 단계별로 복원하여 재실행 부작용을 줄이는 대신 별도 복원 프로토콜을 관리합니다.

**장단점과 검증 범위**

세션을 보관하면 만료까지 자원이 필요합니다. 단계별 복원에는 시간 초과와 프로토콜 버전 처리가 추가되고, 새로운 지속 효과마다 복원 규칙을 정의해야 합니다.

[ProjectVectorServer/Session.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/Session.cpp), [ProjectVectorServer/ReconnectJournal.h](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h), [projectVector/NetworkReconnect.cs](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Network/NetworkReconnect.cs)

### 사망 기록을 고정하고 조회 조건을 검사하는 이유

**구현 흐름**

CombatReportService는 실제 적용된 피해를 방어막과 체력으로 나누어 기록하고, 마지막 사망 피해를 기록한 뒤 보고서를 고정합니다. 조회 시 같은 방인지, 인증된 사망 플레이어인지, 관전자가 아닌지 확인하며 응답은 요청한 플레이어에게만 보냅니다.

**이 구조가 필요한 이유**

사망을 일으킨 피해보다 먼저 기록을 고정하면 가장 필요한 마지막 사건이 빠집니다. 피해 기록 후 고정하면 경기가 계속되어도 사망 UI는 일정한 시점의 기록을 보여줄 수 있습니다. 화면에서 버튼만 숨겨서는 살아 있는 클라이언트의 조회 요청을 막을 수 없으므로 서버에서도 접근 조건을 검사합니다.

**대안과 현재 구현의 절충**

현재 피해 누적값을 바로 읽으면 간단하지만 사망 화면을 보는 중에도 내용이 달라질 수 있습니다. 모든 전투 이벤트를 저장하면 분석 범위는 넓어지는 대신 저장량과 정보 제공 범위를 관리해야 합니다. 사망 시점의 보고서를 고정하면 UI에 필요한 범위의 안정된 데이터를 접근 규칙과 함께 제공할 수 있습니다.

**장단점과 검증 범위**

피해 기록이 사망 알림보다 먼저 처리되도록 순서를 유지해야 합니다. 스킬이 시전자보다 오래 남을 수 있어 원래 객체를 찾지 못할 때도 대체 식별 정보로 피해 기록을 유지합니다.

[ProjectVectorServer/CombatReportService.h](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/CombatReportService.h), [ProjectVectorServer/CombatReportTests.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/Tests/CombatReportTests.cpp)

### 씬의 형상을 서버 데이터로 옮기는 방법

**구현 흐름**

ServerColliderExporter는 ServerCollision 레이어의 BoxCollider를 골라 중심을 월드 좌표로 바꾸고 크기에 스케일을 적용한 뒤 X/Z 경계를 파일로 기록합니다. Unity 씬 정보를 서버가 읽을 수 있는 간단한 데이터로 바꾸는 에디터 작업입니다.

**이 구조가 필요한 이유**

맵은 Unity에서 제작하지만 서버는 Unity 씬을 불러오지 않고 충돌 정보를 필요로 합니다. 필요한 형상을 추출하면 장애물 좌표 목록을 따로 손으로 관리하는 일을 줄이고, 각 수치가 어느 씬 정보에서 나왔는지 추적할 수 있습니다.

**대안과 현재 구현의 절충**

서버 경계를 손으로 작성하면 처음에는 간단하지만 씬과 값이 달라질 수 있습니다. 씬과 물리 형상을 폭넓게 옮기면 더 많은 형태를 다루는 대신 복잡도가 커집니다. 현재 추출기는 박스 기반의 평면 경계라는 좁은 형식을 사용합니다.

**장단점과 검증 범위**

추출기는 회전된 박스의 모든 꼭짓점을 변환하지 않습니다. 회전된 지형을 지원하려면 변환된 경계를 계산해야 하며, 지역 설정에 영향받지 않는 수치 형식과 맵 버전 확인도 필요합니다.

[projectVector/ServerColliderExporter.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Editor/ServerColliderExporter.cs)

### 훈련장에서도 실제 무기 코드를 사용하는 이유

**구현 흐름**

PracticeWeaponInput은 발사를 Weapon.OnUse에 맡기고 발사와 명중 이벤트를 받아 통계를 계산합니다. PracticeRangeController는 훈련 조건과 세션 진행을 별도로 관리합니다. 최고 기록 키에는 과제의 난이도를 바꾸는 조건을 포함하며, 무기 교체나 탄약 보충 시 기록 자격을 해제합니다.

**이 구조가 필요한 이유**

발사 경로를 공유하면 반동과 탄약, 재장전 규칙을 훈련장용으로 다시 구현할 필요가 없습니다. 훈련 진행을 별도로 관리하면 무기 규칙을 복제하지 않고 훈련 모드를 바꿀 수 있습니다. 조건별 기록은 무제한 탄약이나 다른 방어구, 다른 무기로 얻은 결과를 같은 과제의 점수처럼 비교하는 것을 막습니다.

**대안과 현재 구현의 절충**

훈련 전용 무기를 따로 만들면 독립적으로 조정하기 쉽지만 규칙이 중복되고 실제 게임과 달라질 수 있습니다. 게임 무기를 재사용하면 동작을 공유하는 대신 로컬 통계와 훈련 옵션을 구분하는 연습용 컨텍스트가 필요합니다.

**장단점과 검증 범위**

기록은 기기 내 PlayerPrefs에 저장합니다. 해당 기기의 연습 피드백에는 사용할 수 있지만 서버 검증이나 기기 간 동기화는 없습니다. 무기 코드를 공유하므로 게임 쪽 변경이 훈련장에도 영향을 줄 수 있어 두 경로를 함께 확인해야 합니다.

[projectVector/PracticeWeaponInput.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeWeaponInput.cs), [projectVector/PracticeRangeController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeRangeController.cs), [projectVector f4457e2d](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43)

## 실행, 코드 확인

Windows C++ 워크로드가 설치된 Visual Studio에서 [ProjectVectorServer/ProjectVectorServer.sln](ProjectVectorServer/ProjectVectorServer.sln)을 여세요. 구성, 데이터 경로, 의존성, 접속 설정을 확인한 뒤 대응하는 클라이언트를 실행하세요. 기존 계정 데이터를 보존하세요.

[projectVector](https://github.com/YSL-RyuDo/projectVector)

## 상세 문서

[Docs/VoiceChatSetup.md](Docs/VoiceChatSetup.md)

[Docs/CombatReportProtocol.md](Docs/CombatReportProtocol.md)

## 관련 저장소

[ProjectVectorServer](https://github.com/YSL-RyuDo/ProjectVectorServer-public), 공개 소스

[projectVector](https://github.com/YSL-RyuDo/projectVector), 공개 소스
