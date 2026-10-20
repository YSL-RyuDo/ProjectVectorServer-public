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
