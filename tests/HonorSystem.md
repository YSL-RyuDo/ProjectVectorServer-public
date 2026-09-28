# Honor system

## UI and authority

The editable `HonorPanel` is saved in the Unity Lobby scene as a sibling of the profile, career, chip-craft and level-reward panels. Its four sections are description, grade rewards, change history and violation guidance. Hovering previews a grade without changing the player's actual grade. Leaving restores the actual grade. Voice-manner assessment is a neutral **Normal / not yet implemented** display; no voice capture or moderation is performed.

The server owns score and reward eligibility. No client packet can set honor or submit a violation. Guidance and perk cards are informational; automatic chat/cheat detection and gameplay restrictions are not implemented by this feature.

## Project-specific point algorithm

This is a custom algorithm, not a claim to reproduce Strinova's undisclosed formula. The supplied reference defines the six visible thresholds; official update notes describe reputation gains and deductions but do not disclose the full formula.

| Grade | Name | Minimum |
| --- | --- | ---: |
| 1 | 위험 | 0 |
| 2 | 주의 필요 | 301 |
| 3 | 보통 | 601 |
| 4 | 양호 | 901 |
| 5 | 우수 | 1201 |
| 6 | 탁월 | 1601 |

- New and previously uninitialized accounts start at 601. Existing honor data is preserved. Maximum 1800, minimum 0.
- Completed matchmade games: 3–9 minutes +8; 10+ minutes +20; less than 3 minutes +0. Private rooms grant none. Maximum +60 per UTC day. Wins and mechanical skill do not affect honor.
- Disconnect during an active matchmade game: −100. Observers, private rooms and finished-result screens are excluded. There is no reconnect grace system yet; a transport loss counts as a disconnect.
- Trusted server-side violation API: confirmation refusal −50, disruption −160, chat violation −120, match manipulation −240. These reasons are available for future trusted moderation integration; they are not inferred from client claims or automatically detected today.
- Grade 5 one-time reward: currency0 ×1000, currency1 ×2. Grade 6: currency0 ×1500, currency1 ×3. Claiming grade 6 also grants unclaimed grade 5. Falling and returning to the grade never resets the claim marker.

## Persistence / protocol

`career_user_honor.csv` stores score, UTC daily cap accounting and the newest 50 changes in one atomically replaced snapshot. `career_honor_history.csv` is a human-readable mirror; the snapshot is authoritative. Old four-column honor files migrate on load. The wallet supports old three/four-column schemas and atomically saves both balances and `claimedHonorGrade`.

Packets 330/331 request/return self honor state (10 changes/page, at most 5 pages). Packets 332/333 claim/return grade rewards. Successful receipts reuse the common reward popup with sprite, quantity and confirmation.

## Verification

- `tests/HonorTests.cpp`: default Normal, boundaries, private-room exclusion, +60 cap, heavy penalty, reload, cumulative reward/duplicate rejection, 50-record retention, zero floor and save-failure rollback. Fixtures are isolated temporary directories, never live player data.
- `tests/LevelRewardTests.cpp`: existing level-reward regression test.
- Unity `HonorPanelVerification.Run`: serializes/reloads the scene, validates hover/restore and reward states, shows the common receipt, checks five-page history and right-only content drag, validates HonorPanel script/font references, and captures each section into `.test-output/honor-*.png` in the sibling server repo. It does not claim to be a live two-client match/network test.

## Manual multiplayer check

Start the newly built server and client together. Open Profile → Honor. Complete a matchmade game and reopen Honor to check the earned record. With isolated test accounts, verify an in-progress disconnect and a result-screen disconnect separately. Verify grade-5/6 claims, receipt confirmation and reconnect persistence. Do not edit a live account's balance to perform this test.

Reference: https://www.strinova.com/en-US/news/PDheCB8_hbjNfZ_J58-znQ
