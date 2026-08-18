# Career CSV schema

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

`career_matches.csv.matchType` uses `0` for normal/private-room matches and `1`
for ranked matches. Match-history request filters use `0` for all matches, `1`
for normal/private-room matches, and `2` for ranked matches.

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
