#define NOMINMAX
#include <Windows.h>
#include "../ProjectVectorServer/ProjectVectorServer/ShopService.h"
#include <cassert>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    assert(argc == 3); // Existing catalog path and a NEW, isolated test directory.
    const std::filesystem::path root = argv[2];
    assert(!std::filesystem::exists(root));
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(argv[1], root / "shop_catalog.csv");
    VectorShop::Engine engine(root);
    auto initial = engine.Execute("qwer", 0, 0, 1, 0, 0);
    assert(initial.status == 0 && initial.account.coin0 == 1000000 && initial.account.coin1 == 10000 && initial.account.owned.empty());
    auto other = engine.Execute("another-user", 0, 0, 1, 0, 0);
    assert(other.status == 0 && other.account.coin0 == 0 && other.account.coin1 == 0);
    assert(engine.Execute("another-user", 1, 0, 1, 0, 1).status == 3);
    assert(engine.Execute("qwer", 1, 0, 11, 0, 1).status == 2);
    auto draw = engine.Execute("qwer", 1, 1, 10, 0, 100);
    assert(draw.status == 0 && draw.rewards.size() == 10 && draw.account.coin1 == 9990);
    auto retry = engine.Execute("qwer", 1, 1, 10, 0, 100);
    assert(retry.status == 0 && retry.account.history.size() == 10 && retry.account.coin0 == draw.account.coin0 && retry.account.coin1 == 9990);
    for (int i = 0; i < 10; ++i) assert(draw.rewards[i].item == retry.rewards[i].item);
    VectorShop::Engine restarted(root);
    auto reload = restarted.Execute("qwer", 1, 1, 10, 0, 100);
    assert(reload.account.coin1 == 9990 && reload.account.history.size() == 10);
    int unowned = 0, price = 0;
    for (const auto& item : engine.Items()) if (!draw.account.owned.count(item.id)) { unowned = item.id; price = item.price; break; }
    assert(unowned);
    auto bought = engine.Execute("qwer", 2, 0, 1, unowned, 101);
    assert(bought.status == 0 && bought.account.coin0 == draw.account.coin0 - price && bought.account.owned.count(unowned));
    assert(engine.Execute("qwer", 2, 0, 1, unowned, 102).status == 4);
    VectorShop::Result a, b;
    std::thread first([&] { a = engine.Execute("qwer", 1, 1, 10, 0, 103); });
    std::thread second([&] { b = engine.Execute("qwer", 1, 1, 10, 0, 103); });
    first.join(); second.join();
    assert(a.status == 0 && b.status == 0 && a.account.coin1 == 9980 && b.account.coin1 == 9980);
    assert(a.account.history.size() == 21 && b.account.history.size() == 21);
    for (size_t total : {size_t(0), size_t(1), size_t(10), size_t(11), size_t(20), size_t(21), size_t(1000000)})
        for (int requested : {-1, 0, 1, 2, 9999})
        {
            const auto window = VectorShop::VisibleHistory(total, requested);
            assert(window.pages == (total <= 10 ? 1 : 2));
            assert(window.page >= 0 && window.page < window.pages && window.count <= 10);
            const int visible = static_cast<int>((std::min)(total, size_t(20)));
            assert(window.count == (std::min)(10, visible - window.page * 10));
        }
    // A hidden, older-than-20 transaction still retries without another debit.
    auto oldRetry = engine.Execute("qwer", 1, 1, 10, 0, 100);
    assert(oldRetry.status == 0 && oldRetry.rewards.size() == 10 && oldRetry.account.coin1 == 9980);
    assert(oldRetry.account.history.size() == 21);
    const auto lastPage = VectorShop::VisibleHistory(oldRetry.account.history.size(), 9999);
    assert(lastPage.page == 1 && lastPage.pages == 2 && lastPage.count == 10);
    // A failed atomic replace must not consume currency or return uncommitted rewards.
    auto blocked = root / "shop_accounts" / "71776572.txt.tmp";
    std::filesystem::create_directory(blocked);
    auto failed = engine.Execute("qwer", 1, 1, 1, 0, 104);
    assert(failed.status == 5 && failed.rewards.empty() && failed.account.coin1 == 9980);
    std::cout << "PASS: isolated balances, validation, draws, purchases, persistence, idempotency, concurrent retry, save failure\n";
}
