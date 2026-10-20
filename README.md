# CodeName VAIS / Server

[English](README.md), [Korean](README.ko.md), [Japanese](README.ja.md)

A team-built multiplayer FPS. I connect the C++ server, match state and Unity player experience.

| Project | Details |
|---|---|
| Status | Active development |
| Stack | C++, Windows IOCP, C#, Unity |
| Period and team | Mar 2026 ~ Present, 10 members: 5 software, 5 graphics |
| My role | Server development & Unity multiplayer integration |

[Explore the case](https://ysl-ryudo.github.io/en/work/vector/), [Portfolio](https://ysl-ryudo.github.io/en/)

[![CodeName VAIS](https://i.ytimg.com/vi/WqRBn-yWVdo/hqdefault.jpg)](https://www.youtube.com/watch?v=WqRBn-yWVdo)

[Lobby UI](https://www.youtube.com/watch?v=WqRBn-yWVdo), [Store system](https://www.youtube.com/watch?v=OBFWkZE2rzo), [Profile system](https://www.youtube.com/watch?v=A3CkQvnQEGI), [Earlier Project Vector demo](https://www.youtube.com/watch?v=oga4ai_1Ugg)

## My contribution

Implemented IOCP connection handling, sessions and packet dispatch for the game client.

Worked on room/match lifecycle, profile and career data, social features, shop services, and their client contracts.

Added reconnect presentation-state recovery and party/team voice authorization. ReconnectJournal stores persistent state instead of replaying damage, rewards or player commands.

## Contribution evidence

[IOCP](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/IocpCore.cpp)

[ReconnectJournal](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h)

[Validation sources](https://github.com/YSL-RyuDo/ProjectVectorServer/tree/3066fe6d90fb4a56a6c440af56c4e119188e73fc/Tests)

Commits are evidence of the stated changes, not a percentage-of-authorship score. Third-party assets and teammates' work remain separately credited.

## Implementation and design

A multiplayer FPS has to connect transport, match lifecycle and player-facing state. Rejoining a session also requires deciding what state to restore and what actions must not run again.

I worked across the IOCP server and Unity integration: sessions and packet dispatch, match/profile/shop flows, and reconnect presentation-state recovery. ReconnectJournal retains persistent state while excluding damage, rewards and player-command replay.

| Area | Implementation |
|---|---|
| Transport | IOCP / sessions / packet dispatch |
| Game services | Rooms, match lifecycle, career, profile, shop and social data |
| Unity integration | Gameplay packets, lobby/results, Sentinel abilities and voice permissions |
| Recovery boundary | Latest persistent presentation state; gameplay actions excluded |

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

These notes connect the inspected implementation to the constraints it addresses and its tradeoffs. Explanations of design consequences are code analysis, not a claim that every alternative was prototyped. Contribution links identify my changes within team projects; source links retain their original access permissions.

### A purchase must survive a lost response

**Implementation**

The shop request carries a request ID. The server checks its saved transaction history before charging, then computes rewards and saves currency, ownership and transaction history together through CareerManager. A retry returns the recorded rewards. On a save failure, the in-memory wallet is restored and the operation reports failure.

**Design rationale**

Disabling a purchase button only blocks repeated clicks in that UI. It cannot distinguish an unprocessed request from a completed transaction whose response was lost. Persisting the request ID with the result makes that distinction possible after reconnecting or restarting. Sharing the existing wallet also avoids separate shop and match-reward balances drifting apart.

**Alternatives and the current tradeoff**

Button locking is simple but only local. Deduplicating in memory also handles repeated requests, but loses that knowledge on restart. The current persisted request/result record covers that case at the cost of ledger maintenance. A database transaction with a unique request key is another option, with a database deployment and migration cost.

**Tradeoffs and verification**

The mutex serializes these operations, so persistence time can delay other work using the same lock. The UI exposes only 20 recent transactions, but the deduplication ledger is retained; that display limit is not a storage bound. SharedShopWalletTests covers restart retries, concurrent duplicate requests and save-failure rollback. Those are checked-in test cases, not a new load-test result.

[ProjectVectorServer/ShopService.h](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/ShopService.h), [ProjectVectorServer/CareerManager.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/CareerManager.cpp), [ProjectVectorServer/SharedShopWalletTests.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/Tests/SharedShopWalletTests.cpp), [projectVector/ShopGachaImplementation.md](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Docs/ShopGachaImplementation.md)

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

Retaining sessions consumes resources until expiry, while staged recovery adds timeout and protocol-version handling. New persistent effects must be added to the restoration contract. The cited client is the YSL branch and the server is main; the links identify separate inspected revisions, not proof that every combination has passed an end-to-end match.

[ProjectVectorServer/Session.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/Session.cpp), [ProjectVectorServer/ReconnectJournal.h](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h), [projectVector/NetworkReconnect.cs](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Network/NetworkReconnect.cs)

### A death report is a snapshot with access rules

**Implementation**

CombatReportService records applied damage split between barrier and health. It freezes the report after recording the fatal hit. Requests require the matching room, a dead authenticated player and a non-observer role, and the response goes only to that player.

**Design rationale**

Freezing before the fatal hit would omit the most relevant event. Freezing afterward gives the death UI a stable snapshot even as the match continues. Server-side access checks matter because hiding a UI button alone cannot prevent a living client from asking for combat information.

**Alternatives and the current tradeoff**

Reading a live damage accumulator is convenient but its contents can change while the death screen is open. Retaining every raw combat event allows broader analysis but increases storage and disclosure concerns. A frozen per-death report gives the UI a narrower, stable dataset with explicit access rules.

**Tradeoffs and verification**

Damage recording and death notification must keep the same order as combat code evolves. Skills can outlive their owners, so fallback identity handling preserves a damage entry even when the original actor is no longer available. This is a report-access boundary, not a claim that every form of information leakage is solved.

[ProjectVectorServer/CombatReportService.h](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/CombatReportService.h), [ProjectVectorServer/CombatReportTests.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/Tests/CombatReportTests.cpp)

## Open and inspect

Open [ProjectVectorServer/ProjectVectorServer.sln](ProjectVectorServer/ProjectVectorServer.sln) in Visual Studio with the Windows C++ workload. Inspect the selected configuration, data paths, dependencies and endpoint settings before starting the matching client. Preserve existing account data.

[projectVector](https://github.com/YSL-RyuDo/projectVector)

## Development and verification scope

Active development. Test sources cover reconnect, match lifecycle, shop and social/voice contracts; their presence alone is not a current passing test report. Consult Docs/VoiceChatSetup.md for the recorded voice checks and remaining two-PC tests. Current TCP transport is not TLS-protected; the documented development setup uses a trusted LAN/VPN. Do not interpret IOCP usage as a measured scalability result.

## Further documentation

[Docs/VoiceChatSetup.md](Docs/VoiceChatSetup.md)

[Docs/CombatReportProtocol.md](Docs/CombatReportProtocol.md)

## Related repositories

[ProjectVectorServer](https://github.com/YSL-RyuDo/ProjectVectorServer), Private, access required

[projectVector](https://github.com/YSL-RyuDo/projectVector), Private, access required
