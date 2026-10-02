// Included by CareerManager.cpp: shares its wallet lock and atomic persistence.
CareerManager::PassResult CareerManager::ExecutePass(const std::string& userId, int operation, int index, int64_t expectedWeek)
{
    std::lock_guard<std::mutex> lock(mutex_);
    PassResult out{};
    auto found = wallets_.find(userId);
    if (found == wallets_.end() || operation < 0 || operation > 2) return out;
    auto& wallet = found->second;
    const auto previous = wallet;
    const int64_t now = static_cast<int64_t>(std::time(nullptr)), day = now / 86400;
    const int64_t week = ((day + 3) / 7) * 7 - 3;
    int version = 1, xp = 0, rewards = 0, quests = 0, visits = 0;
    int64_t joined = now, savedWeek = week, visitDay = -1;
    if (!wallet.passState.empty())
    {
        std::istringstream input(wallet.passState);
        if (!(input >> version >> joined >> savedWeek >> visitDay >> visits >> xp >> rewards >> quests) ||
            version != 1 || joined < 0 || joined > now || xp < 0 || xp > 2900 || visits < 0 || visits > 7 ||
            rewards < 0 || rewards > 0x3fffffff || quests < 0 || quests > 63) return out;
    }
    if (savedWeek != week) { savedWeek = week; visits = 0; quests &= ~7; }
    if (visitDay != day) { visits = std::min(7, visits + 1); visitDay = day; }
    int weeklyMatches = 0, totalMatches = 0, combat = 0;
    std::unordered_set<uint64_t> counted;
    for (const auto& player : matchPlayers_)
    {
        if (player.userId != userId || !counted.insert(player.matchId).second) continue;
        auto match = matches_.find(player.matchId);
        if (match == matches_.end() || match->second.startedAtUnix < joined || match->second.durationSeconds <= 0) continue;
        totalMatches = std::min(30, totalMatches + 1);
        combat = std::min(50, combat + std::max(0, player.kills) + std::max(0, player.assists));
        if (match->second.startedAtUnix >= week * 86400) weeklyMatches = std::min(7, weeklyMatches + 1);
    }
    const int progress[] = {visits, weeklyMatches, weeklyMatches, totalMatches, totalMatches, combat};
    const int targets[] = {3, 3, 7, 10, 30, 50};
    out.result = 0;
    if (operation == 1)
    {
        if (index < 0 || index >= 6 || (index < 3 && expectedWeek != week) ||
            (index >= 0 && index < 6 && ((quests & (1 << index)) || progress[index] < targets[index]))) out.result = 2;
        else { quests |= 1 << index; xp = std::min(2900, xp + 100); }
    }
    if (operation == 2)
    {
        if (index < 1 || index > 30 || index > 1 + xp / 100 || (index >= 1 && index <= 30 && (rewards & (1 << (index - 1))))) out.result = 2;
        else
        {
            const int coin0 = index % 5 == 0 ? 0 : 300, coin1 = index % 5 == 0 ? 3 : 0;
            if (wallet.freeCurrency > std::numeric_limits<int>::max() - coin0 || wallet.victoryCurrency > std::numeric_limits<int>::max() - coin1) out.result = 2;
            else { wallet.freeCurrency += coin0; wallet.victoryCurrency += coin1; rewards |= 1 << (index - 1); }
        }
    }
    std::ostringstream state;
    state << 1 << ' ' << joined << ' ' << week << ' ' << visitDay << ' ' << visits << ' ' << xp << ' ' << rewards << ' ' << quests;
    wallet.passState = state.str();
    if (wallet.passState != previous.passState && !SaveWalletsUnlocked()) { wallet = previous; out.result = 3; return out; }
    out.level = 1 + xp / 100; out.xp = xp; out.rewardMask = rewards; out.questMask = quests;
    out.currency0 = wallet.freeCurrency; out.currency1 = wallet.victoryCurrency; out.week = week;
    std::copy(progress, progress + 6, out.progress);
    return out;
}
