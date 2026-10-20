# CodeName VAIS / Server

<!-- portfolio-languages -->
[![English (current)](https://ysl-ryudo.github.io/assets/diagrams/language-en-current.svg)](README.md) [![한국어](https://ysl-ryudo.github.io/assets/diagrams/language-ko.svg)](README.ko.md) [![日本語](https://ysl-ryudo.github.io/assets/diagrams/language-ja.svg)](README.ja.md)
<!-- /portfolio-languages -->

A team-built multiplayer FPS. I connect the C++ server, match state and Unity player experience.

| Project | Details |
|---|---|
| Status | Active development |
| Stack | C++, Windows IOCP, C#, Unity |
| Period and team | Mar 2026 ~ Present, 10 members: 5 software, 5 graphics |
| My role | C++ server, Unity game flow, UI and tooling |

[Explore the case](https://ysl-ryudo.github.io/en/work/vector/), [Portfolio](https://ysl-ryudo.github.io/en/)

[![CodeName VAIS](https://i.ytimg.com/vi/WqRBn-yWVdo/hqdefault.jpg)](https://www.youtube.com/watch?v=WqRBn-yWVdo)

[Lobby UI](https://www.youtube.com/watch?v=WqRBn-yWVdo), [Store system](https://www.youtube.com/watch?v=OBFWkZE2rzo), [Profile system](https://www.youtube.com/watch?v=A3CkQvnQEGI), [Earlier Project Vector demo](https://www.youtube.com/watch?v=oga4ai_1Ugg)

## Inside the game

[![Moving-target practice uses the gameplay weapon, recoil and ammunition paths.](https://ysl-ryudo.github.io/assets/gameplay/vais-practice.webp)](https://ysl-ryudo.github.io/assets/gameplay/vais-practice.webp)

Moving-target practice uses the gameplay weapon, recoil and ammunition paths.

[![Character, weapon and target settings determine the training session and its record conditions.](https://ysl-ryudo.github.io/assets/gameplay/vais-loadout.webp)](https://ysl-ryudo.github.io/assets/gameplay/vais-loadout.webp)

Character, weapon and target settings determine the training session and its record conditions.

[Watch gameplay](https://ysl-ryudo.github.io/en/work/vector/#gameplay), CodeName VAIS practice range, YSL branch, 0:53. Recorded in Unity with scripted inputs on 20 October 2026.

## What I built across the game

I built C++ server services, Unity match flow, profile and social features, reward presentation and editor tools. I also connected character abilities and combat state to the network protocol.

[![My implementation flow](https://ysl-ryudo.github.io/assets/diagrams/vector-flow-en.svg)](https://ysl-ryudo.github.io/assets/diagrams/vector-flow-en.svg)

### Rooms, teams and match entry

Implemented room creation, entry, exit, slot changes, slot-swap responses, leader transfer and kicking on the server. Character locking and loading readiness form separate steps before gameplay; PickManager connects selection input and received state to the Unity screen.

[RoomManager.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/RoomManager.cpp), [My change 20f7bde](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/a2ab47ba2f3eff317df5ce0e2a056e7dd0926f17), [PickManager.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Pick/PickManager.cs), [My change 8fcd6f1](https://github.com/YSL-RyuDo/projectVector/commit/8fcd6f1e91fbcd627a852de66db88b444884ccea)

### Combat and objective protocol

Implemented server handlers for firing, reloading, weapon switching, damage and death, plus spike pickup, drop, planting and defusing. These handlers connect player actions to room and session state.

[PacketDispatcher.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/PacketDispatcher.cpp), [My change 3066fe6](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/f3c4884a4270746434464630226e662fbe5fd1e6), [Session.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/Session.cpp), [My change 3066fe6](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/f3c4884a4270746434464630226e662fbe5fd1e6)

### Character ability integration

Extended Sentinel abilities with device and area state synchronization and a dedicated C-skill network manager. The server also routes ability requests and events for other character families.

[SentinelCNetworkManager.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Skill/SentinelCNetworkManager.cs), [My change 756c9a0](https://github.com/YSL-RyuDo/projectVector/commit/756c9a09f32ed4a073d5d3d60008b1aaa44f0f60), [SentinelSkill_E.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/Character/Sentinel/SentinelSkill_E.cs), [My change 5e5c3c5](https://github.com/YSL-RyuDo/projectVector/commit/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350)

### Selection and loading presentation

Built selection presentation, map introduction and loading presentation around the match flow. These are client features in their own right: the selected character, team slots and transition screens have to remain coherent while network readiness advances.

[PickSelectionPresentation.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Pick/PickSelectionPresentation.cs), [My change 76d985b](https://github.com/YSL-RyuDo/projectVector/commit/76d985b6bd9dff1fb7b4f08002e64f98d9bb8bc3), [MatchLoadingPresentation.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Pick/MatchLoadingPresentation.cs), [My change 8fcd6f1](https://github.com/YSL-RyuDo/projectVector/commit/8fcd6f1e91fbcd627a852de66db88b444884ccea)

### Career and progression screens

Implemented career presentation and profile career data, with server-side match history, ranking and progression state. Level-reward claims connect eligibility and saved ownership to the client reward screen rather than ending at a visual unlock.

[CareerPanelController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/CareerPanelController.cs), [My change 76d985b](https://github.com/YSL-RyuDo/projectVector/commit/76d985b6bd9dff1fb7b4f08002e64f98d9bb8bc3), [MyPageLevelRewardController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/MyPageLevelRewardController.cs), [My change 955101d](https://github.com/YSL-RyuDo/projectVector/commit/955101d782e975c1812ed94de893e4b263a266e3), [CareerManager.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/CareerManager.cpp), [My change 39de6fd](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/b6d1fe1f5c794f027e93025a8d03e748365d5f59)

### Profile editing and ChipCraft

Built profile customization including draggable stat slots and ChipCraft medal placement. The server represents medal layouts as bounded data with position, rotation, scale and ownership checks; the Unity side provides the manipulation and presentation.

[MyPageChipCraftController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/MyPageChipCraftController.cs), [My change 06f82cc](https://github.com/YSL-RyuDo/projectVector/commit/06f82cc44d00c33af781710f0f2ace3d10353969), [MyPageStatDropSlot.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/MyPageStatDropSlot.cs), [My change 632e516](https://github.com/YSL-RyuDo/projectVector/commit/632e516b428b768b7eabed586afc89bfcd154af3), [ProfileManager.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/ProfileManager.cpp), [My change f65f26d](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/edc2517f7c4cfdfee0594a869e5bdacdd544cccb)

### Friends and player relationships

Built the friend panel and list models and the server's requests, acceptance, cancellation, blocking, memo and presence flows. Friendship and directional actions such as a request or block have different data relationships; they are not interchangeable list entries.

[FriendPanelController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/FriendPanelController.cs), [My change 06f82cc](https://github.com/YSL-RyuDo/projectVector/commit/06f82cc44d00c33af781710f0f2ace3d10353969), [FriendManager.cpp (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/FriendManager.cpp), [My change 675d37f](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/07860d73d4ee93b800e32de05300832d8c0a6cc8)

### Honor and affinity

Implemented honor UI and claim-related activity tracking, alongside affinity state and dialogue-completion processing. Reward presentation is connected to server conditions and persisted progression instead of relying on the screen to decide ownership.

[MyPageHonorController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/MyPageHonorController.cs), [My change f4457e2](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43), [HonorActivity.h (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/HonorActivity.h), [My change 39de6fd](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/b6d1fe1f5c794f027e93025a8d03e748365d5f59), [AffinityService.h (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/AffinityService.h), [My change 39de6fd](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/b6d1fe1f5c794f027e93025a8d03e748365d5f59)

### Purchases and reward presentation

Implemented transaction retry handling and the Unity gacha sequence, including skip and cleanup. The implementation notes below trace the boundary between a confirmed reward and its cancellable presentation, including failure and duplicate-request cases.

[ShopService.h (main)](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/8b52b5f17e25d329f9e624e0543e0bfac4c07c5e/ProjectVectorServer/ProjectVectorServer/ShopService.h), [My change 39de6fd](https://github.com/YSL-RyuDo/ProjectVectorServer-public/commit/b6d1fe1f5c794f027e93025a8d03e748365d5f59), [GachaManager.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/Shop/GachaManager.cs), [My change 8563836](https://github.com/YSL-RyuDo/projectVector/commit/856383681df4b817b62db98cbf8c4f01b98ba270)

### World-data export and build tooling

Built Unity editor exporters for server collision, ground and height-volume data, as well as project build tooling. This production work connects authored scenes to runtime data and makes repeated preparation less dependent on manually copying geometry values.

[ServerColliderExporter.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Editor/ServerColliderExporter.cs), [My change 70d856f](https://github.com/YSL-RyuDo/projectVector/commit/70d856fcd5b1502db05f90d25c5dacdb90315d76), [ServerGroundExporter.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Editor/ServerGroundExporter.cs), [My change 70d856f](https://github.com/YSL-RyuDo/projectVector/commit/70d856fcd5b1502db05f90d25c5dacdb90315d76), [SafePlayerBuild.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Editor/SafePlayerBuild.cs), [My change 632e516](https://github.com/YSL-RyuDo/projectVector/commit/632e516b428b768b7eabed586afc89bfcd154af3)

### Practice range and comparable records

Built character and weapon selection, five training modes, distance and armor settings, and practice statistics. The range uses the gameplay weapon path for firing, recoil, ammunition and reloading. Records separate weapon, character, difficulty and target conditions; changing equipment during a run makes that run ineligible for a best record.

[PracticeRangeController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeRangeController.cs), [My change f4457e2](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43), [PracticeLoadout.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeLoadout.cs), [My change f4457e2](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43), [PracticeWeaponInput.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeWeaponInput.cs), [My change f4457e2](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43)

## Contribution evidence

[IOCP](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/f3c4884a4270746434464630226e662fbe5fd1e6/ProjectVectorServer/ProjectVectorServer/IocpCore.cpp)

[ReconnectJournal](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/f3c4884a4270746434464630226e662fbe5fd1e6/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h)

[Validation sources](https://github.com/YSL-RyuDo/ProjectVectorServer-public/tree/f3c4884a4270746434464630226e662fbe5fd1e6/Tests)

## Implementation and design

A multiplayer FPS has to connect transport, match lifecycle and player-facing state. Rejoining a session also requires deciding what state to restore and what actions must not run again.

I worked across the IOCP server and Unity integration: sessions and packet dispatch, match/profile/shop flows, and reconnect presentation-state recovery. ReconnectJournal retains persistent state while excluding damage, rewards and player-command replay.

### ReconnectJournal

**Choose recoverable state**

Observe accepts an explicit set of persistent presentation packets. Damage, rewards and player commands do not enter the recovery journal.

**Keep the latest version**

Entries are keyed by packet type and entity ID. A subsequent update replaces that entry instead of appending a replayable action history.

**Respect lifecycle changes**

New-match/round packets clear the journal. Removal packets erase the corresponding object state, and mutually exclusive states replace earlier alternatives.

**Restore remaining time**

Snapshot subtracts elapsed steady-clock time from stored durations and clamps at zero. Zero-duration status packets are retained to clear stale client state.

CodeName VAIS is the official name; Project Vector is the former working title. Repository names are retained.

## How the code works and why it is structured this way

[![Purchase and reward presentation](https://ysl-ryudo.github.io/assets/diagrams/vector-design-en.svg)](https://ysl-ryudo.github.io/assets/diagrams/vector-design-en.svg)

A repeated request reuses its saved result. A failed save rolls the transaction back. Skip and close operate on presentation after reward confirmation.

### A purchase must survive a lost response

**Implementation**

The shop request carries a request ID. The server checks its saved transaction history before charging, then computes rewards and saves currency, ownership and transaction history together through CareerManager. A retry returns the recorded rewards. On a save failure, the in-memory wallet is restored and the operation reports failure.

**Design rationale**

Disabling a purchase button only blocks repeated clicks in that UI. It cannot distinguish an unprocessed request from a completed transaction whose response was lost. Persisting the request ID with the result makes that distinction possible after reconnecting or restarting. Sharing the existing wallet also avoids separate shop and match-reward balances drifting apart.

**Alternatives and the current tradeoff**

Button locking is simple but only local. Deduplicating in memory also handles repeated requests, but loses that knowledge on restart. The current persisted request/result record covers that case at the cost of ledger maintenance. A database transaction with a unique request key is another option, with a database deployment and migration cost.

**Tradeoffs and verification**

Operations sharing the lock wait for persistence, so slow saves can stall other requests. The UI shows the latest 20 transactions, while the deduplication ledger keeps older entries and continues to grow. SharedShopWalletTests covers retries after restart, concurrent duplicates and save-failure rollback.

[ProjectVectorServer/ShopService.h](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/ShopService.h), [ProjectVectorServer/CareerManager.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/CareerManager.cpp), [ProjectVectorServer/SharedShopWalletTests.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/Tests/SharedShopWalletTests.cpp), [projectVector/ShopGachaImplementation.md](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Docs/ShopGachaImplementation.md)

### Reward presentation has its own lifetime

**Implementation**

The Unity client starts GachaManager only after receiving the server result. The coroutine advances through preparation, movement, impacts, reveal and summary with unscaled time. Skip stops the coroutine, releases temporary objects, RenderTextures and materials, then shows the same confirmed result. Saved scene references control the UI; temporary 3D preview objects are created separately.

**Design rationale**

A cinematic is cancellable; a confirmed purchase is not another roll. Keeping those lifetimes separate lets skip and close change presentation without granting or cancelling rewards. Unscaled timing keeps the sequence moving when gameplay time is paused. Scene-authored UI also keeps layout editing available without running generation code.

**Alternatives and the current tradeoff**

One coroutine keeps timing and cleanup together. Timeline would expose the sequence visually to content authors; tweens would express individual motion more compactly. The current coroutine favors code-level control, but has less visual authoring support. All three still need explicit cancellation and must consume the same server-confirmed reward.

**Tradeoffs and verification**

Coroutines keep this sequence in code, but phase order, cancellation and cleanup must be maintained explicitly. A Timeline or tween-based alternative could improve authoring, yet seek and skip would still need the same purchase boundary. Unity garbage collection alone does not replace explicit cleanup of these rendering resources.

[projectVector/GachaManager.cs](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/Shop/GachaManager.cs), [projectVector/ShopGachaImplementation.md](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Docs/ShopGachaImplementation.md)

### Reconnect restores a match, not just a socket

**Implementation**

The server retains an authenticated playing session for a limited recovery window and attaches a new transport to that logical session. A generation counter invalidates stale disconnect cleanup. On the Unity side, NetworkReconnect waits for scene loading and player objects, then acknowledges restoration stages before normal input resumes. ReconnectJournal keeps the latest persistent presentation state rather than replaying damage or rewards.

**Design rationale**

Transport identity and player identity have different lifetimes. Creating a fresh player on every reconnect would lose match state; applying packets before objects exist would leave the client only partly restored. Explicit phases make those dependencies visible. A latest-state journal also avoids treating an old gameplay action as a new command.

**Alternatives and the current tradeoff**

Starting a new session is simpler but does not preserve the ongoing match. Full event replay can reconstruct history only if replay semantics are carefully controlled. The current design instead retains the logical session and restores selected state in phases, reducing replay side effects while adding a recovery protocol to maintain.

**Tradeoffs and verification**

Retaining a session consumes resources until expiry. Staged restoration adds timeout and protocol-version handling, and each new persistent effect needs a restoration rule.

[ProjectVectorServer/Session.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/Session.cpp), [ProjectVectorServer/ReconnectJournal.h](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h), [projectVector/NetworkReconnect.cs](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Network/NetworkReconnect.cs)

### A death report is a snapshot with access rules

**Implementation**

CombatReportService records applied damage split between barrier and health. It freezes the report after recording the fatal hit. Requests require the matching room, a dead authenticated player and a non-observer role, and the response goes only to that player.

**Design rationale**

Freezing before the fatal hit would omit the most relevant event. Freezing afterward gives the death UI a stable snapshot even as the match continues. Server-side access checks matter because hiding a UI button alone cannot prevent a living client from asking for combat information.

**Alternatives and the current tradeoff**

Reading a live damage accumulator is convenient but its contents can change while the death screen is open. Retaining every raw combat event allows broader analysis but increases storage and disclosure concerns. A frozen per-death report gives the UI a narrower, stable dataset with explicit access rules.

**Tradeoffs and verification**

Damage records must precede death notifications. Skills may outlive their caster, so fallback identity data preserves attribution when the original object is unavailable.

[ProjectVectorServer/CombatReportService.h](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/ProjectVectorServer/ProjectVectorServer/CombatReportService.h), [ProjectVectorServer/CombatReportTests.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer-public/blob/7224b09befff5905b81e643648e1ffb90caac4d9/Tests/CombatReportTests.cpp)

### Exporting scene geometry into server data

**Implementation**

ServerColliderExporter selects BoxColliders on the ServerCollision layer, transforms their centers, scales their sizes and writes X/Z bounds. This is an editor-time conversion from Unity scene data to a compact server input.

**Design rationale**

The map is authored in Unity while the server needs collision information without loading the Unity scene. Exporting selected geometry avoids maintaining a second hand-written list of obstacle coordinates and makes the source of those values inspectable.

**Alternatives and the current tradeoff**

Hand-authored server bounds are simple initially but can drift from the scene. A full scene/physics representation supports more shapes at much greater complexity. This exporter chooses a narrow box-based planar representation.

**Tradeoffs and verification**

The exporter does not transform every corner of a rotated box. Supporting rotated geometry requires transformed bounds; locale-independent number formatting and map-version checks are further integration points.

[projectVector/ServerColliderExporter.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Editor/ServerColliderExporter.cs)

### A practice range should exercise the real weapon

**Implementation**

PracticeWeaponInput delegates firing to Weapon.OnUse and receives shot and hit events for statistics. PracticeRangeController manages training conditions and session progress separately. Its best-record key includes the conditions that change the task; a weapon change or refill invalidates record eligibility.

**Design rationale**

Sharing the weapon path keeps practice tied to the same recoil, ammo and reload rules as gameplay. Separating training progress lets modes change without duplicating those rules. Condition-specific records prevent results from unlimited ammo, different armor or different weapons from competing as if they were the same task.

**Alternatives and the current tradeoff**

A separate practice-only weapon is easy to tune in isolation, but duplicates rules and can drift from the game. Reusing the gameplay weapon preserves behavior while requiring an explicit practice context for local statistics and training options.

**Tradeoffs and verification**

Records are local PlayerPrefs values. They provide feedback on that device but have no server-side integrity check or cross-device synchronization. Sharing weapon code also means a gameplay change can affect the range, so weapon and training behavior need to be checked together.

[projectVector/PracticeWeaponInput.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeWeaponInput.cs), [projectVector/PracticeRangeController.cs (YSL)](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/WeaponTest/PracticeRangeController.cs), [projectVector f4457e2d](https://github.com/YSL-RyuDo/projectVector/commit/f4457e2d77c251b9e0da5d976927303d04a64b43)

## Open and inspect

Open [ProjectVectorServer/ProjectVectorServer.sln](ProjectVectorServer/ProjectVectorServer.sln) in Visual Studio with the Windows C++ workload. Inspect the selected configuration, data paths, dependencies and endpoint settings before starting the matching client. Preserve existing account data.

[projectVector](https://github.com/YSL-RyuDo/projectVector)

## Further documentation

[Docs/VoiceChatSetup.md](Docs/VoiceChatSetup.md)

[Docs/CombatReportProtocol.md](Docs/CombatReportProtocol.md)

## Related repositories

[ProjectVectorServer](https://github.com/YSL-RyuDo/ProjectVectorServer-public), Public source

[projectVector](https://github.com/YSL-RyuDo/projectVector), Public repository
