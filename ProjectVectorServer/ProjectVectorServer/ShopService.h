#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>

// Ownership stays isolated from gameplay unlocks; production uses the shared CareerWallet.
namespace VectorShop
{
struct Item { int id, kind, model, skin, grade, price; std::string name; };
struct Record { unsigned long long request; long long time; int item, duplicate, refund; };
struct Account { int coin0 = 0, coin1 = 0; std::map<int, int> owned; std::vector<Record> history; };
struct Result { int status = 0; Account account; std::vector<Record> rewards; };

class Engine
{
    std::filesystem::path root;
    std::mutex mutex;
    std::mt19937 random{std::random_device{}()};
    std::vector<Item> catalog;
    bool valid = false;

    std::filesystem::path AccountPath(const std::string& user) const
    {
        static const char hex[] = "0123456789abcdef";
        std::string name;
        for (unsigned char c : user) { name += hex[c >> 4]; name += hex[c & 15]; }
        return root / "shop_accounts" / (name + ".txt");
    }
    Account Load(const std::string& user)
    {
        Account a;
        const auto path = AccountPath(user);
        if (!std::filesystem::exists(path))
        {
            if (user == "qwer") { a.coin0 = 1000000; a.coin1 = 10000; }
            return a;
        }
        std::ifstream in(path);
        return ReadAccount(in);
    }
    static Account ReadAccount(std::istream& in)
    {
        Account a;
        std::string tag;
        int version;
        if (!(in >> tag >> version >> a.coin0 >> a.coin1) || tag != "VECTOR_SHOP" || version != 1 || a.coin0 < 0 || a.coin1 < 0)
            throw std::runtime_error("Invalid shop account");
        while (in >> tag)
        {
            if (tag == "O") { int id, count; if (!(in >> id >> count) || count < 1) throw std::runtime_error("Invalid ownership"); a.owned[id] = count; }
            else if (tag == "H") { Record r{}; if (!(in >> r.request >> r.time >> r.item >> r.duplicate >> r.refund)) throw std::runtime_error("Invalid history"); a.history.push_back(r); }
            else throw std::runtime_error("Invalid shop record");
        }
        return a;
    }
    bool Save(const std::string& user, const Account& a)
    {
        const auto path = AccountPath(user);
        std::filesystem::create_directories(path.parent_path());
        const auto temp = path.string() + ".tmp";
        std::ofstream out(temp, std::ios::trunc);
        out << "VECTOR_SHOP 1 " << a.coin0 << ' ' << a.coin1 << '\n';
        for (const auto& entry : a.owned) out << "O " << entry.first << ' ' << entry.second << '\n';
        for (const auto& r : a.history) out << "H " << r.request << ' ' << r.time << ' ' << r.item << ' ' << r.duplicate << ' ' << r.refund << '\n';
        out.flush();
        if (!out.good()) return false;
        out.close();
#ifdef _WIN32
        return MoveFileExA(temp.c_str(), path.string().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::filesystem::rename(temp, path);
        return true;
#endif
    }
public:
    static std::string Encode(const Account& account)
    {
        std::ostringstream out;
        out << "VECTOR_SHOP 1 " << account.coin0 << ' ' << account.coin1 << ';';
        for (const auto& entry : account.owned) out << "O " << entry.first << ' ' << entry.second << ';';
        for (const auto& r : account.history) out << "H " << r.request << ' ' << r.time << ' ' << r.item << ' ' << r.duplicate << ' ' << r.refund << ';';
        return out.str();
    }
    Account SharedAccount(const std::string& user, const std::string& saved, int coin0, int coin1)
    {
        Account account;
        if (saved.empty()) account = Load(user); // Import ownership/history once; never import the old test wallet.
        else { auto text = saved; std::replace(text.begin(), text.end(), ';', '\n'); std::istringstream in(text); account = ReadAccount(in); }
        account.coin0 = coin0; account.coin1 = coin1;
        return account;
    }
    explicit Engine(std::filesystem::path directory = ".") : root(std::move(directory))
    {
        std::ifstream in(root / "shop_catalog.csv");
        std::string line;
        std::getline(in, line);
        std::array<int, 3> counts{};
        while (std::getline(in, line))
        {
            if (line.empty()) continue;
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream row(line);
            Item item{};
            if (!(row >> item.id >> item.kind >> item.model >> item.skin >> item.grade >> item.price)) return;
            std::getline(row >> std::ws, item.name);
            if (item.grade < 0 || item.grade > 2 || item.price <= 0 || item.name.empty() || item.name.size() > 63 ||
                std::any_of(catalog.begin(), catalog.end(), [&](const Item& i) { return i.id == item.id; })) return;
            catalog.push_back(item); ++counts[item.grade];
        }
        valid = catalog.size() <= 100 && counts[0] && counts[1] && counts[2];
    }
    const std::vector<Item>& Items() const { return catalog; }
    Result Execute(const std::string& user, int operation, int currency, int count, int itemId, unsigned long long request, Account* shared = nullptr)
    {
        std::lock_guard<std::mutex> guard(mutex);
        Result result;
        if (user.empty() || !valid) { result.status = 1; return result; }
        try
        {
            result.account = shared ? *shared : Load(user);
            if (operation == 0) { if (!shared && !Save(user, result.account)) result.status = 5; return result; }
            if ((operation != 1 && operation != 2) || !request || currency < 0 || currency > 1 || (count != 1 && count != 10))
            { result.status = 2; return result; }
            for (const auto& r : result.account.history) if (r.request == request) result.rewards.push_back(r);
            if (!result.rewards.empty()) return result; // Retry/reconnect must never charge twice.
            Account next = result.account;
            const Item* purchase = nullptr;
            int cost = count * (currency == 0 ? 300 : 1);
            if (operation == 2)
            {
                for (const auto& i : catalog) if (i.id == itemId) purchase = &i;
                if (!purchase || currency != 0 || count != 1) { result.status = 2; return result; }
                if (next.owned.count(itemId)) { result.status = 4; return result; }
                cost = purchase->price;
            }
            int& balance = currency == 0 ? next.coin0 : next.coin1;
            if (balance < cost) { result.status = 3; return result; }
            balance -= cost;
            const long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            for (int draw = 0; draw < count; ++draw)
            {
                const Item* selected = purchase;
                if (!selected)
                {
                    int roll = std::uniform_int_distribution<int>(0, 99)(random);
                    int grade = roll < 5 ? 0 : roll < 29 ? 1 : 2;
                    std::vector<const Item*> pool;
                    for (const auto& i : catalog) if (i.grade == grade) pool.push_back(&i);
                    selected = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(random)];
                }
                const bool duplicate = next.owned.count(selected->id) != 0;
                const int refund = duplicate ? (selected->grade == 0 ? 150 : selected->grade == 1 ? 75 : 30) : 0;
                next.coin0 += refund;
                next.owned[selected->id] = 1;
                Record r{request, now, selected->id, duplicate ? 1 : 0, refund};
                next.history.push_back(r); result.rewards.push_back(r);
            }
            // Keep the complete transaction ledger for idempotency; UI paginates newest records.
            if (!shared && !Save(user, next)) { result.status = 5; result.rewards.clear(); return result; }
            if (shared) *shared = next;
            result.account = std::move(next);
        }
        catch (...) { result.status = 5; result.rewards.clear(); }
        return result;
    }
};
}
