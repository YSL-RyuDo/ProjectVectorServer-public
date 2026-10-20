# CodeName VAIS — Server

[English](README.md) · [Korean](README.ko.md) · [Japanese](README.ja.md)

A team-built multiplayer FPS. I connect the C++ server, match state and Unity player experience.

**C++ · Windows IOCP · C# · Unity** · Active development

**Mar 2026 - Present · 10 members: 5 software, 5 graphics**

Server development & Unity multiplayer integration

[Explore the case](https://ysl-ryudo.github.io/en/work/vector/) · [Portfolio](https://ysl-ryudo.github.io/en/)

[ProjectVectorServer](https://github.com/YSL-RyuDo/ProjectVectorServer) (Private · access required) · [projectVector](https://github.com/YSL-RyuDo/projectVector) (Private · access required)

## System responsibilities

| Area | Implementation |
|---|---|
| Transport | IOCP / sessions / packet dispatch |
| Game services | Rooms, match lifecycle, career, profile, shop and social data |
| Unity integration | Gameplay packets, lobby/results, Sentinel abilities and voice permissions |
| Recovery boundary | Latest persistent presentation state; gameplay actions excluded |

## ReconnectJournal

After a reconnect, what should the player see—and which gameplay actions must never be repeated?

1. **Choose recoverable state** — Observe accepts an explicit set of persistent presentation packets. Damage, rewards and player commands do not enter the recovery journal.
2. **Keep the latest version** — Entries are keyed by packet type and entity ID. A subsequent update replaces that entry instead of appending a replayable action history.
3. **Respect lifecycle changes** — New-match/round packets clear the journal. Removal packets erase the corresponding object state, and mutually exclusive states replace earlier alternatives.
4. **Restore remaining time** — Snapshot subtracts elapsed steady-clock time from stored durations and clamps at zero. Zero-duration status packets are retained to clear stale client state.

CodeName VAIS is the official name; Project Vector is the former working title. Repository names are retained.

## Development videos

- [Lobby UI](https://www.youtube.com/watch?v=WqRBn-yWVdo)
- [Store system](https://www.youtube.com/watch?v=OBFWkZE2rzo)
- [Profile system](https://www.youtube.com/watch?v=A3CkQvnQEGI)
- [Earlier Project Vector demo](https://www.youtube.com/watch?v=oga4ai_1Ugg)

## My contribution

- Implemented IOCP connection handling, sessions and packet dispatch for the game client.
- Worked on room/match lifecycle, profile and career data, social features, shop services, and their client contracts.
- Added reconnect presentation-state recovery and party/team voice authorization. ReconnectJournal stores persistent state instead of replaying damage, rewards or player commands.

## Contribution evidence

- [IOCP](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/IocpCore.cpp)
- [ReconnectJournal](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h)
- [Validation sources](https://github.com/YSL-RyuDo/ProjectVectorServer/tree/3066fe6d90fb4a56a6c440af56c4e119188e73fc/Tests)

Commits are evidence of the stated changes, not a percentage-of-authorship score. Third-party assets and teammates' work remain separately credited.

## Open and inspect

Open [ProjectVectorServer/ProjectVectorServer.sln](ProjectVectorServer/ProjectVectorServer.sln) in Visual Studio with the Windows C++ workload. Inspect the selected configuration, data paths, dependencies and endpoint settings before starting the matching client. Preserve existing account data.

[projectVector](https://github.com/YSL-RyuDo/projectVector)

## Development and verification scope

Active development. Test sources cover reconnect, match lifecycle, shop and social/voice contracts; their presence alone is not a current passing test report. Consult Docs/VoiceChatSetup.md for the recorded voice checks and remaining two-PC tests. Current TCP transport is not TLS-protected; the documented development setup uses a trusted LAN/VPN. Do not interpret IOCP usage as a measured scalability result.

## Further documentation

- [Docs/VoiceChatSetup.md](Docs/VoiceChatSetup.md)
- [Docs/CombatReportProtocol.md](Docs/CombatReportProtocol.md)
