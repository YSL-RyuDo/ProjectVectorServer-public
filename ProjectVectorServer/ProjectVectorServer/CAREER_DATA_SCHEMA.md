# Career CSV schema

## 장시간 미참여 명예 판정 (2026-10-10)

`HonorActivity.h`의 상수로 최초 운영 기준을 분리했다. 실전 데이터를 보고 조정할 초기 기준이며 매크로/고의 트롤 판별기는 아니다.

- 일반/랭크 매칭의 살아 있는 실제 전투 참가자만 1초 간격으로 관찰한다. 구매/준비, 사망/관전, 기절, 라운드 정산, 경기 종료, 사설방/연습장은 관찰 시간에서 제외한다.
- 0.5m 이상 위치 변화 또는 3도 이상 조준 변화, 서버가 수락한 발사/직접 피해/스킬 소비/폭탄 설치·해제 행동을 활동으로 취급한다. 동일 위치 패킷, 작은 좌표 떨림, 단순 채팅/핑 수신만으로는 활동으로 인정하지 않는다. 드론 조종자는 드론 위치와 조준을 사용한다.
- 연속 60초 이상 활동 없는 구간을 장기 미참여로 집계한다. **관찰 시간 180초 이상, 장기 미참여 합계 120초 이상, 2개 이상 라운드, 관찰 시간 대비 60% 이상**을 모두 만족해야 판정한다. 60초 도달 시 해당 구간 전체 시간을 집계한다. 라운드/사망/제외 상태 전환 시 연속 구간을 끊는다. 관찰 간격이 3초를 넘는 서버 정지 구간은 증거로 사용하지 않는다.
- 완료 시 기존 +8/+20 명예 대신 -50점(reason 8 `장시간 미참여`)을 경기당 한 번 반영한다. 0점 아래로 내려가지 않는다. 킬/승패/명중률은 판정 기준이 아니다. 랭크, 경험치, 호감도, 재화, 강제 퇴장 규칙은 바꾸지 않는다.
- 완료와 접속 종료는 세션의 동일 명예 정산 게이트를 사용하여 경기 이탈 감점과 중복 적용하지 않는다. 이미 나간 참가자를 소급 판정하지 않는다. 알 수 없는/부족한 활동 증거는 자동 감점하지 않는다.
- `career_user_honor.csv` 내 history 항목은 기존 `time:delta:reason:matchId`를 계속 읽으며 새 저장에서는 뒤에 `eligibleSeconds:longIdleSeconds:idleRounds`를 붙인다. 기존 점수·내역은 유지한다. 최신 50건 제한 및 원자적 저장/실패 롤백 유지. 보조 `career_honor_history.csv`는 기존 5열 요약 형식 유지, 판정 근거는 권위 스냅샷에 저장된다. 이전 서버 버전은 새 7항목 내역을 읽지 못하므로 운영 교체/롤백 시 파일 백업이 필요하다.
- 패킷 번호와 구조 크기는 그대로이며 사유 코드 8만 추가했다. 클라이언트 기존 명예 내역 행에 문구를 표시한다. 씬/프리팹 변경이나 런타임 UI 생성은 없다.

검증: `HonorActivityTests.cpp`는 연속 미참여, 단일 라운드 수비 대기, 0킬 활동, 이동/조준/떨림, 제외 구간, 사망, 서버 정지, 복귀, 정산 경합을 확인한다. `HonorHistoryTests.cpp`는 실제 감점/보상 제외, 근거 저장·재시작 및 구형 내역 보존을 확인한다. 공용 테스트 프로젝트의 `/p:RegressionSource=HonorActivityTests.cpp`로 활동 판정 테스트를 빌드한다. 테스트 데이터와 출력은 저장소 밖에 둔다.

## 명예 기록 및 상점 기록 표시 범위 (2026-10-10)

- 일반/랭크 매칭 경기 180초 이상 완료: 기존 +8점, 600초 이상 +20점. 하루 획득 상한 60점, 총점 상한 1,800점 유지. 상한으로 지급이 0이어도 완료 시각·사유·실제 증감·경기 ID를 기록한다.
- 기존 경기 중 이탈 -100점 유지. 수락창에서 거절/이탈한 당사자는 -50점(reason 3), 30초 동안 미수락한 당사자는 -50점(reason 7)으로 기존 명예 저장 경로에 연결했다.
- 매칭 대기열 취소, 다른 참가자의 거절 때문에 취소된 사용자, 정상 수락 후 기다린 사용자는 감점하지 않는다. 방 생성 자체 실패도 감점하지 않는다.
- Ready token은 기존 매칭 잠금 안에서 처리 후 제거하여 중복·만료 응답이 재감점되지 않는다. 수락창 생성 시 계정 ID를 보관해 연결 종료 이후에도 해당 계정만 기록한다.
- 명예 내역은 기존 최신 50건(5페이지) 제한 유지. 패킷 크기/번호는 변경하지 않고 reason 7만 추가했다. 클라이언트는 `입장 확인 시간 초과`로 표시한다.
- 장시간 미참여 보강은 위 별도 절을 따른다. 고의 방해 자동 판정은 없으며, 저장 실패는 롤백하고 로그를 남긴다. 실패 이벤트의 영속 재시도 큐는 없다.
- 상점은 최신 20건(10건씩 최대 2페이지)만 전송·표시한다. 거래 원장은 요청 ID 재시도 시 재결제 방지에 사용하므로 삭제/축약하지 않는다. 이번 제한은 화면·전송 범위이며, 저장 용량 상한/아카이빙은 별도 설계 대상이다.

검증: `Tests/HonorHistoryTests.cpp`(완주/상한/사유 7 재로딩/저장 실패/50건),
`Tests/ShopServiceTests.cpp`(0·10·11·20·21건 페이지 경계, 숨겨진 거래 재시도),
`Tests/LobbySocialIntegration.py`(실제 TCP 거절/미응답 대상 구분·재전송·대기열 취소 면제).
단위 테스트 빌드 프로젝트는 `Tests/HonorShopRegressionTests.vcxproj`이며 기본 명예 테스트,
`/p:RegressionSource=ShopServiceTests.cpp`로 상점 테스트를 빌드한다. 출력·중간 경로는 저장소 밖으로 지정한다.
명예 실행 인자는 새 임시 폴더 하나, 상점 실행 인자는 카탈로그 경로와 새 임시 폴더다. 기존 계정 폴더에서는 실행하지 않는다.

## 호감도 서버 / Unity 담당자 인수인계 (2026-10-10)

이번 변경은 서버만 구현한다. 기존 Unity 씬, 프리팹, 대화 본문, 버튼은 수정하지 않는다.
클라이언트 담당자가 기존 UI를 보존하면서 연결한다. 신규 UI는 반드시 Play 전에
씬/프리팹 Hierarchy에 저장해야 하며 실행 중 대체 화면/버튼을 생성하지 않는다.

### 성장과 저장 규칙

- 일반/랭크 자동 매칭에서 180초 이상 진행된 경기를 종료 시점까지 남아 완주한
  플레이어의 **해당 경기 사용 캐릭터**만 성장한다. 사설방/연습장/관전자/중도 이탈 제외.
- 패배/무승부 완주 10점, 승리 완주 15점. 연승/연패 보정과 호감도 감소 없음.
- 레벨 0~5의 누적 기준점은 0 / 100 / 250 / 500 / 850 / 1300.
  `affinity_rules.csv`에서 점수·최소 경기 시간·기준점을 조정하고 서버를 재시작한다.
  이 수치는 첫 기본값이며 밸런스 확정값은 아니다.
- 호감도는 캐릭터 숙련도/캐릭터 랭킹/계정 경험치/재화와 별개다.
- 기존 `character_affinity.csv`의 c0~c3는 0~5 레벨로 읽어 초기값을 보존한다.
  새 저장이 존재하면 그것이 우선이다. 기존 파일은 호감도 기능에서 쓰지 않는다.
- 새 파일 `character_affinity_progress.dat`에 계정별 4캐릭터 점수/레벨, 최근 지급 경기 ID,
  revision, 대화 완료·선택지를 함께 저장한다. 임시 파일 후 원자적 교체하며 실패하면
  메모리 변경도 취소한다. 손상된 저장/설정은 덮어쓰지 않고 오류를 반환한다.
- 과거 경기 점수 소급 지급 없음. 동일/이전 경기 ID 재지급 없음. 대화를 읽었다고
  호감도나 재화를 지급하지 않는다. 클라이언트가 점수/승패/대상 계정을 지정할 수 없다.
- 최종 저장 실패는 서버 로그에 남는다. 현재는 지급 실패를 재시작 후 자동 복구하는
  지급 대기열까지 구현한 것은 아니다. 저장 오류를 방치하지 말 것.

### 캐릭터 연결 (기존 BackgroundStoryData 기준)

| 서버 characterId | 이름 | 기존 CSV | 대화 ID 접두사 |
| --- | --- | --- | --- |
| 0 | 한별 / Initiator | H_dialogue.csv | H |
| 1 | 세린 / Controller | S_dialogue.csv | S |
| 2 | 하은 / Sentinel | B_dialogue.csv | B |
| 3 | 도희 / Entry | D_dialogue.csv | D |

현재 Lobby의 DialogueManager 네 항목이 모두 characterId=0으로 저장돼 있다.
**배열 순서를 ID로 쓰지 말고 위 표대로 Inspector 값을 연결할 것.**
서버 `AffinityCatalog.h`는 기존 대화의 148개 블록 ID와 해금 레벨/선택지 분기를
담고 있다. 대화 본문은 Unity의 기존 에셋을 사용한다. ID·레벨·분기를 변경하면
서버 카탈로그와 클라이언트를 함께 검증·배포해야 한다.

### 패킷 규격

모든 정수는 little-endian, pack=1. 공통 헤더는 `uint16 size, uint16 type`이며
size는 헤더 포함 길이다. 아래 offset은 **전체 패킷 기준**이다.
NetworkManager처럼 헤더를 제외한 body를 받는 파서는 offset에서 4를 빼면 된다.
구조체와 static_assert는 `AffinityPackets.h` 참조.

| 번호 | 방향 | 이름 | 전체 길이 |
| --- | --- | --- | --- |
| 324 | C→S | 기존 호감도 레벨 조회 | 4 |
| 325 | S→C | 기존 호감도 레벨 목록 | 5 + count×8 |
| 366 | C→S | 선택 캐릭터 상세·대화 진행 조회 | 12 |
| 367 | S→C | 상세 상태 응답 | 31 + count×33 |
| 368 | C→S | 대화 블록 완료·선택지 저장 | 49 |
| 369 | S→C | 저장 결과와 최신 상세 상태 | 31 + count×33 |

기존 363/364 생일, 365 매칭 카드는 변경하지 않는다.
새 366~369 enum/수신 처리는 Unity 담당자가 추가한다.

- **324**: 헤더만. 기존 `SendFavorLevelRequest()` 그대로 사용 가능.
- **325**: offset 4 `uint8 count`, offset 5부터 `{int32 characterId, int32 level}`.
  성공 count=4, 인증/저장 오류 count=0. 기존 패킷에는 상세 오류 필드가 없으므로
  상세 화면에서는 366/367을 사용할 것.
- **366**: offset 4 `uint32 requestId`, offset 8 `int32 characterId`.
- **368**: offset 4 `uint32 requestId`, offset 8 `uint32 expectedRevision`,
  offset 12 `int32 characterId`, offset 16 `char dialogueId[32]`, offset 48 `uint8 choice`.
  문자열은 ASCII ID, 최대 31바이트와 NUL, 나머지는 0 채움.
  choice는 선택지 없는 블록=0, 선택지 있는 블록=1/2/3. nextDialogueId를 보내는 것이 아니다.
- **367/369 공통**:
  offset 4 requestId(u32), 8 result(u8), 9 revision(u32), 13 characterId(i32),
  17 level(i32), 21 누적 points(i32), 25 다음 레벨 누적 기준점(i32, 최대 레벨이면 0),
  29 dialogueCount(u16), 31부터 `{char dialogueId[32], uint8 choice}` 반복.
  응답의 대화 목록은 해당 캐릭터가 완료한 블록만 포함한다.
  현재 카탈로그는 캐릭터당 37블록, 최대 전체 응답 1252바이트다.

결과 코드: 0 성공, 1 잘못된 값, 2 미인증/계정 없음, 3 저장/설정 오류,
4 미해금 대화, 5 없는 대화 또는 다른 캐릭터 ID, 6 잘못된 선택지,
7 revision 불일치, 8 이미 확정한 선택지와 충돌.

같은 블록·같은 선택지 재전송은 성공으로 처리하며 중복 저장하지 않는다.
이미 확정한 선택지는 다른 요청으로 덮어쓰지 않는다. revision은 **계정 전체** 기준이라
다른 캐릭터 진행이나 경기 완료로도 증가한다. 오류 7이면 최신 상태를 반영하고 다시
판단한다. 저장 성공 응답 전에 로컬 완료/선택 기록을 확정하지 않는다.
잘못된 패킷 길이는 무시한다. 인증 계정은 세션에서 결정하며 targetUserId 필드는 없다.

### Unity 담당자가 맡을 구현 범위

1. 기존 대화/초상화/선택지 에셋을 유지하면서 위 캐릭터 ID 연결을 수정한다.
2. 화면 진입 시 366 조회 → 서버 레벨/점수/진행으로 표시한다.
   로그인/캐릭터 전환 후 늦게 도착한 응답은 requestId·계정·characterId로 구분한다.
3. 레벨에 따라 기존 대화를 해금하고 완료된 블록은 서버 목록으로 복원한다.
   분기 선택은 기존 nextDialogueId로 재생하되 서버에는 choice 번호를 저장한다.
4. `DialogueManager.Start()` 전에 응답이 오면 사라질 수 있으므로
   네트워크 상태 캐시 후 UI 초기화 시 적용하거나 UI 준비 후 요청한다.
5. 기존 PlayerPrefs는 계정 ID가 없으므로 서버의 완료 기록으로 취급하거나
   임의 업로드하지 않는다. 보존하되 신규 진행의 권위 데이터로 사용하지 않는다.
6. 고정 캐릭터 버튼은 Hierarchy에 저장한다. 기존 `BuildCharacterList()`의
   자식 전체 삭제/재생성으로 새 화면을 구성하지 않는다. UI 디자인은 담당자가 진행한다.
7. 계정 A→B 교체, 캐릭터 전환 중 응답, 재접속, 저장 실패, 연속 클릭,
   해금 전 접근, 선택지 재전송을 확인한다. 숙련도·사설방·상점·생일 UI는 유지한다.

### 검증 실행

`python -B Tests/ValidateAffinityContract.py <Unity 프로젝트 경로>`는 Unity 파일을 수정하지
않고 기존 대화 카탈로그·캐릭터 매핑·패킷 번호·길이를 대조하는 정적 검사다.

`Tests/AffinityServiceTests.vcxproj`를 Debug x64로 빌드한다. OutDir/IntDir은 저장소
밖으로 지정하고 생성된 EXE에는 **아직 존재하지 않는 임시 디렉터리**를 인자로 넘긴다.
승리/패배/연패, 지급 제외, 중복·동시 요청, 계정·캐릭터 분리, 기존 레벨 보존,
대화 검증, 재시작, 저장 실패 복구, 손상 파일 보호를 검사한다.

`python -B Tests/AffinityIntegration.py <새 서버 EXE 절대 경로>`는 임시 계정으로
TCP 조회/저장/재접속과 기존 CSV 보존을 검사한다. 9000 포트가 사용 중이면 시작하지 않는다.
둘 다 기존 사용자 데이터에서 실행하지 않는다. 서버 바이너리 실행이 OS 정책으로
차단되면 정책을 우회하지 말고 실행 가능한 개발 환경에서 검증한다.

### 연습장 확장 담당자 참고 (이번 서버 작업에서는 구현하지 않음)

Unity 저장소의 기존 `EnemySpawner`/`DifficultyTarget`/`EnemyDummy`와 WeaponTest 씬을
출발점으로 삼는다. 이미 난이도·표적 노출 시간·30회 사격·점수가 있으므로 중복 구현하지 않는다.
확인한 보유 에셋 활용 후보:

- `Assets/FPS_Bundle/FPS_Map0/Prefabs/Box_01.prefab`, `Box_02.prefab`, `Barrier_01.prefab`:
  엄폐물과 사격 레인 경계. 실제 크기·콜라이더는 배치 전 확인.
- `Assets/Sci-Fi Styled Modular Pack/Prefabs/Floors`와 `Walls`, `Doors`:
  기존 게임 분위기에 맞는 실내 훈련실. 한 공간에서 재질/축척을 맞추고 과도하게 섞지 않는다.
- 기존 FPS_Weapon0/1 모델보다 우선 프로젝트에서 실제 사용하는 WeaponData/발사·반동을
  연결해 실전과 연습의 감각 차이를 줄인다. 모델만 교체해서 기능이 같다고 가정하지 않는다.

추천 첫 범위는 로비 진입/복귀 → 기존 30회 훈련 유지 → 거리/이동 표적 →
명중률·반응 시간·개인 최고 기록. 이후 캐릭터 교체/쿨타임 초기화로 확장한다.
기본 EditorBuildSettings에 WeaponTest/Tutorial이 없으므로 빌드 진입 동선도 확인한다.
신규 UI는 전부 Hierarchy에 저장하고 기존 씬/참조/계정 기록을 보존한다.

The CSV files model normalized relational tables. `userId` references
`users.csv.id`; `seasonId`, `matchId`, and `itemId` reference the corresponding
parent tables.

| File | Key | Purpose |
| --- | --- | --- |
| `career_seasons.csv` | `seasonId` | Season definition and active period |
| `career_rank_tiers.csv` | `tierId` | Rank point ranges, rank image IDs, and leaderboard-only tier rules |
| `career_season_stats.csv` | `(userId, seasonId)` | One aggregate row per user and season |
| `career_matchmaking_ratings.csv` | `(userId, queueType)` | Separate hidden MMR, uncertainty, matches, and wins for normal (`0`) and ranked (`1`) queues |
| `career_character_stats.csv` | `(userId, seasonId, characterId)` | Per-character mastery score, match count, and wins |
| `career_matches.csv` | `matchId` | One row per completed match, including its match type |
| `career_match_players.csv` | `(matchId, userId)` | Match-to-user participant relation and immutable match stats |
| `career_items.csv` | `itemId` | Inventory item catalog |
| `career_user_inventory.csv` | `(userId, itemId)` | User-to-item ownership relation |
| `career_user_wallet.csv` | `userId` | Persistent free and victory currency balances |

`CareerManager::Load()` validates references, removes invalid duplicate rows in
memory, creates missing tables, and creates the active-season stat row for every
existing account. `UserManager::Register()` initializes the same row for a new
account. Empty inventory is represented by no ownership rows.

Normal and ranked matchmaking grant 100 free currency for participation and
one victory currency to winners. Private rooms grant no currency and never
updates MMR. Normal and ranked queues keep independent hidden ratings.

## Matchmaking and rating

- Normal matchmaking uses only the normal hidden MMR. Ranked matchmaking uses
  ranked hidden MMR as the skill estimate and the visible tier as a guardrail.
- Both queues initially accept teams whose calculated win probability is
  between 45% and 55%. The acceptable interval and rating range expand slowly
  with queue time, after the mandatory five-second search period.
- The 3-vs-3 team optimizer evaluates every valid split of the selected six
  players and chooses the split closest to a 50% prediction.
- Parties are never split. A coordinated duo contributes 35 effective MMR and
  a trio contributes 70. The optimizer prefers equal party power on both teams;
  if a solo team must face the stronger party composition, its expected-win
  rating automatically grants more points for a win and removes fewer for a
  loss.
- Ranked visible points use a result-first Elo-style adjustment. An even match
  is about `+20/-20`; an underdog receives more for an upset and loses less,
  while a favorite receives less for an expected win and loses more for an
  upset loss. The result is clamped to 8 through 35 points, with up to three
  additional points derived from the round-score margin and up to six points
  pulling visible rank toward hidden MMR.
- Hidden MMR updates in both queues with uncertainty-based volatility. New and
  migrated accounts begin with high uncertainty, which contracts over their
  first matches. Individual kills or damage do not affect rating, avoiding a
  role bias and stat-farming incentive.

`career_matches.csv.matchType` uses `0` for normal matchmaking, `1` for ranked
matchmaking, and `2` for private rooms. Match-history request filters keep their
existing values: `0` for all matches, `1` for normal/private-room matches, and
`2` for ranked matches.

`career_match_players.csv` stores `headshotKills`, `spikePlants`,
`spikeDefuses`, and `meleeKills` in addition to the original combat totals.
The career-detail packet aggregates these immutable match rows by season, mode,
and selected character. Season `0` is the initial active season; all four
character IDs (`0` through `3`) are returned even when their match count is
zero so the client selector remains complete.

At match completion the server writes the match and participant rows, updates
season aggregates, recalculates rank points, and mirrors the resolved
`rankImageId` into `player_data.csv` so existing lobby and match-loading UI keeps
using the same rank source.

## Rank progression

- Bronze 5 through Diamond 2 use contiguous 100-point ranges.
- Reaching the next range promotes immediately; there is no promotion series.
- Diamond 1 starts at 2400 points and uses `maxPoints = -1` to mean uncapped.
- A row with `leaderboardTop = 30` is not part of normal point progression.
  Eligible Diamond 1 players ranked 1st through 30th receive that row's Master
  display tier dynamically. Falling outside the top 30 displays Diamond 1 again.
- `rankImageId` is data-only. Client image assets remain separately authored.

## Ranking pages

- Overall ranking is ordered by rank points and exposes at most positions 1
  through 100 in pages of ten rows. The last page is calculated from the actual
  result count, so no empty page is exposed.
- Character ranking is ordered independently for each character by accumulated
  mastery points, then wins, fewer matches, and user ID.
- Character mastery increases from match participation, combat contribution,
  wins, and MVP results; it does not replace the overall rank score.
- ID and nickname search runs against the full ranking, so matching accounts
  outside the normal top 100 can still be returned.
