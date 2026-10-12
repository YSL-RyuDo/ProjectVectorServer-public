#pragma once
#include "AffinityCatalog.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

// Server-owned progression. Independent of mastery, currencies and legacy CSVs.
namespace VectorAffinity
{
enum Status : uint8_t { Ok = 0, Invalid = 1, Unauthorized = 2, StorageError = 3,
    Locked = 4, UnknownDialogue = 5, InvalidChoice = 6, StaleRevision = 7, ChoiceConflict = 8 };
struct Rules
{
    int completionPoints = 10, victoryBonus = 5, minimumSeconds = 180;
    std::array<int, 6> thresholds{0, 100, 250, 500, 850, 1300};
};
struct Character { int level = 0, points = 0; };
using DialogueKey = std::pair<int, std::string>;
struct Account
{
    uint32_t revision = 0;
    uint64_t lastMatchId = 0;
    std::array<Character, 4> characters{};
    // Presence means completed. 0=no choice; 1..3=chosen branch in the catalog.
    std::map<DialogueKey, int> dialogues;
};
struct Result { Status status = Ok; Account account; };

class Store
{
public:
    explicit Store(std::filesystem::path root = ".") : root_(std::move(root)) { LoadRules(); Load(); }
    const Rules& Configuration() const { return rules_; }
    bool Healthy() const { return healthy_; }
    int NextPoints(const Character& character) const
    { return character.level >= 5 ? 0 : rules_.thresholds[character.level + 1]; }
    Result Get(const std::string& user, const std::array<int, 4>& legacy)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ValidUser(user)) return {Invalid, {}};
        return {healthy_ ? Ok : StorageError, Current(user, legacy)};
    }
    Result CompleteDialogue(const std::string& user, const std::array<int, 4>& legacy,
        uint32_t expectedRevision, int character, const std::string& dialogueId, int choice)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ValidUser(user) || character < 0 || character >= 4) return {Invalid, {}};
        auto account = Current(user, legacy);
        if (!healthy_) return {StorageError, account};
        const auto* definition = FindDialogue(character, dialogueId);
        if (!definition) return {UnknownDialogue, account};
        if (definition->level > account.characters[character].level) return {Locked, account};
        const bool hasChoices = definition->next[0][0] || definition->next[1][0] || definition->next[2][0];
        if (choice < 0 || choice > 3 || (hasChoices ? choice == 0 || !definition->next[choice - 1][0] : choice != 0))
            return {InvalidChoice, account};
        const DialogueKey key{character, dialogueId};
        const auto found = account.dialogues.find(key);
        // Same completion can safely be retried after a lost reply or reconnect.
        if (found != account.dialogues.end()) return {found->second == choice ? Ok : ChoiceConflict, account};
        if (expectedRevision != account.revision) return {StaleRevision, account};
        if (account.revision == UINT32_MAX) return {StorageError, account};
        account.dialogues[key] = choice;
        ++account.revision;
        return Commit(user, account, legacy);
    }
    // Called ONLY by authoritative match settlement, never by a client packet.
    Result AwardMatch(const std::string& user, const std::array<int, 4>& legacy,
        uint64_t matchId, int character, bool matchmade, int matchType,
        int durationSeconds, bool completed, bool won)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ValidUser(user) || character < 0 || character >= 4 || matchId == 0) return {Invalid, {}};
        auto account = Current(user, legacy);
        if (!healthy_) return {StorageError, account};
        if (!matchmade || (matchType != 0 && matchType != 1) || !completed || durationSeconds < rules_.minimumSeconds)
            return {Ok, account};
        if (matchId <= account.lastMatchId) return {Ok, account};
        if (account.revision == UINT32_MAX) return {StorageError, account};
        auto& state = account.characters[character];
        state.points = (std::max)(state.points, static_cast<int>((std::min)(int64_t(rules_.thresholds[5]),
            int64_t(state.points) + rules_.completionPoints + (won ? rules_.victoryBonus : 0))));
        while (state.level < 5 && state.points >= rules_.thresholds[state.level + 1]) ++state.level;
        account.lastMatchId = matchId;
        ++account.revision;
        return Commit(user, account, legacy);
    }
private:
    static bool ValidUser(const std::string& user)
    { return !user.empty() && user.size() < 20 && user.find_first_of("\r\n\0", 0, 3) == std::string::npos; }
    static bool End(std::istream& input) { input >> std::ws; return input.eof(); }
    template<class T> static bool Unsigned(std::istream& input, T& value)
    {
        std::string token; if (!(input >> token) || token.empty()) return false;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
    }
    Account Current(const std::string& user, const std::array<int, 4>& legacy) const
    {
        auto found = accounts_.find(user);
        if (found != accounts_.end()) return found->second;
        Account account;
        for (int i = 0; i < 4; ++i)
        {
            account.characters[i].level = (std::clamp)(legacy[i], 0, 5);
            account.characters[i].points = rules_.thresholds[account.characters[i].level];
        }
        return account;
    }
    Result Commit(const std::string& user, const Account& next, const std::array<int, 4>& legacy)
    {
        const auto previous = Current(user, legacy);
        const bool existed = accounts_.count(user) != 0;
        accounts_[user] = next;
        if (!Save())
        {
            if (existed) accounts_[user] = previous; else accounts_.erase(user);
            return {StorageError, previous};
        }
        return {Ok, next};
    }
    void LoadRules()
    {
        const auto path = root_ / "affinity_rules.csv";
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error) { healthy_ = false; return; }
        if (!exists) return; // Compiled defaults are identical to the checked-in config.
        std::ifstream input(path);
        std::string line;
        if (!std::getline(input, line)) { healthy_ = false; return; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != "key,value") { healthy_ = false; return; }
        std::map<std::string, int> values;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto comma = line.find(',');
            if (comma == std::string::npos) { healthy_ = false; return; }
            std::istringstream number(line.substr(comma + 1)); int value;
            if (!(number >> value) || !End(number) || !values.emplace(line.substr(0, comma), value).second)
            { healthy_ = false; return; }
        }
        if (input.bad() || values.size() != 9) { healthy_ = false; return; }
        Rules candidate;
        for (int i = 0; i <= 5; ++i)
        {
            const auto found = values.find("level" + std::to_string(i));
            if (found == values.end()) { healthy_ = false; return; }
            candidate.thresholds[i] = found->second;
        }
        if (!values.count("completionPoints") || !values.count("victoryBonus") || !values.count("minimumSeconds"))
        { healthy_ = false; return; }
        candidate.completionPoints = values["completionPoints"]; candidate.victoryBonus = values["victoryBonus"];
        candidate.minimumSeconds = values["minimumSeconds"];
        if (candidate.thresholds[0] != 0 || candidate.thresholds[5] > 100000000 ||
            candidate.completionPoints < 1 || candidate.completionPoints > 10000 || candidate.victoryBonus < 0 ||
            candidate.victoryBonus > 10000 || candidate.minimumSeconds < 1 || candidate.minimumSeconds > 86400)
        { healthy_ = false; return; }
        for (int i = 1; i <= 5; ++i) if (candidate.thresholds[i] <= candidate.thresholds[i-1]) { healthy_ = false; return; }
        rules_ = candidate;
    }
    void Load()
    {
        const auto path = root_ / "character_affinity_progress.dat";
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error) { healthy_ = false; return; }
        if (!exists) return;
        std::ifstream input(path);
        std::string line;
        if (!std::getline(input, line)) { healthy_ = false; return; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != "VECTOR_AFFINITY 1") { healthy_ = false; return; }
        std::map<std::string, Account> loaded;
        while (std::getline(input, line))
        {
            std::istringstream row(line); char kind; std::string user;
            if (!(row >> kind >> std::quoted(user)) || !ValidUser(user)) { healthy_ = false; return; }
            if (kind == 'U')
            {
                Account account;
                if (!Unsigned(row, account.revision) || !Unsigned(row, account.lastMatchId)) { healthy_ = false; return; }
                for (auto& character : account.characters)
                    if (!(row >> character.level >> character.points) || character.level < 0 || character.level > 5 ||
                        character.points < 0 || character.points > 100000000) { healthy_ = false; return; }
                if (!End(row) || !loaded.emplace(user, account).second) { healthy_ = false; return; }
            }
            else if (kind == 'D')
            {
                int character, choice; std::string id;
                if (!(row >> character >> std::quoted(id) >> choice) || !End(row) || !loaded.count(user))
                { healthy_ = false; return; }
                const auto* definition = FindDialogue(character, id);
                if (!definition || definition->level > loaded[user].characters[character].level || choice < 0 || choice > 3)
                { healthy_ = false; return; }
                const bool hasChoices = definition->next[0][0] || definition->next[1][0] || definition->next[2][0];
                if ((hasChoices ? choice == 0 || !definition->next[choice - 1][0] : choice != 0) ||
                    !loaded[user].dialogues.emplace(DialogueKey{character, id}, choice).second)
                { healthy_ = false; return; }
            }
            else { healthy_ = false; return; }
        }
        if (input.bad()) { healthy_ = false; return; }
        accounts_ = std::move(loaded);
    }
    bool Save() const
    {
        const auto path = root_ / "character_affinity_progress.dat";
        const auto temporary = root_ / "character_affinity_progress.dat.tmp";
        std::ofstream output(temporary, std::ios::trunc | std::ios::binary);
        if (!output) return false;
        output << "VECTOR_AFFINITY 1\n";
        for (const auto& [user, account] : accounts_)
        {
            output << 'U' << ' ' << std::quoted(user) << ' ' << account.revision << ' ' << account.lastMatchId;
            for (const auto& c : account.characters) output << ' ' << c.level << ' ' << c.points;
            output << '\n';
            for (const auto& [key, choice] : account.dialogues)
                output << "D " << std::quoted(user) << ' ' << key.first << ' ' << std::quoted(key.second) << ' ' << choice << '\n';
        }
        output.flush(); if (!output.good()) return false;
        output.close(); if (output.fail()) return false;
#ifdef _WIN32
        return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error; std::filesystem::rename(temporary, path, error); return !error;
#endif
    }
    std::filesystem::path root_;
    Rules rules_;
    bool healthy_ = true;
    std::map<std::string, Account> accounts_;
    std::mutex mutex_;
};
}
