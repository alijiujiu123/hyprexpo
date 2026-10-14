#include "../src/OverviewSwipe.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>

namespace {
int failures = 0;
void expect(bool value, const std::string& label) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

struct Calls {
    int ends = 0;
    int resets = 0;
    bool selects = false;
    double projected = 0;
};

struct Session {
    int generation;
    Calls& calls;
    bool committed = false;
    bool closing = true;
    std::function<void()> afterEnd;

    bool closeCommitted() const { return committed; }
    void setClosing(bool value) { closing = value; }
    void resetSwipe() { ++calls.resets; }
    void onSwipeEnd(bool selects, double projected) {
        expect(!closing, "release clears transient closing");
        ++calls.ends;
        calls.selects = selects;
        calls.projected = projected;
        // The callback may delete this, just like an empty COverview::close.
        auto callback = afterEnd;
        if (callback)
            callback();
    }
};

struct Registry {
    std::map<int, std::unique_ptr<Session>> sessions;
    Session& add(int key, int generation, Calls& calls) {
        auto& owner = sessions[key];
        owner = std::make_unique<Session>(Session{generation, calls, false, true, {}});
        return *owner;
    }
    auto resolve(int key, int generation) {
        return [this, key, generation]() -> Session* {
            const auto it = sessions.find(key);
            return it != sessions.end() && it->second->generation == generation ? it->second.get() : nullptr;
        };
    }
};

struct Size { double x, y; };
}

int main() {
    Registry registry;
    Calls normal, empty, replacement, closed, primary, follower;

    registry.add(1, 1, normal);
    Hyprexpo::finishOverviewSwipe(registry.resolve(1, 1), true, 123.5);
    expect(normal.ends == 1 && normal.resets == 1 && normal.selects && normal.projected == 123.5,
           "a surviving primary receives selection, projection and reset");

    auto& destroyed = registry.add(2, 2, empty);
    destroyed.afterEnd = [&] { registry.sessions.erase(2); };
    Hyprexpo::finishOverviewSwipe(registry.resolve(2, 2), false, 3929.76);
    expect(empty.ends == 1 && empty.resets == 0 && !empty.selects,
           "an immediately destroyed follower is never reset (the live crash)");

    auto& replaced = registry.add(3, 3, empty);
    replaced.afterEnd = [&] { registry.add(3, 4, replacement); };
    Hyprexpo::finishOverviewSwipe(registry.resolve(3, 3), false, -1);
    expect(replacement.ends == 0 && replacement.resets == 0,
           "a new generation on the same monitor does not receive the old reset");

    registry.add(4, 5, closed).committed = true;
    Hyprexpo::finishOverviewSwipe(registry.resolve(4, 5), true, 50);
    Hyprexpo::finishOverviewSwipe(registry.resolve(99, 6), true, 50);
    expect(closed.ends == 0 && closed.resets == 0, "committed and missing sessions ignore release");

    registry.add(5, 7, primary);
    auto& other = registry.add(6, 8, follower);
    other.afterEnd = [&] { registry.sessions.erase(5); };
    Hyprexpo::finishOverviewSwipe(registry.resolve(6, 8), false, 20);
    Hyprexpo::finishOverviewSwipe(registry.resolve(5, 7), true, 20);
    expect(follower.ends == 1 && follower.resets == 1 && primary.ends == 0 && primary.resets == 0,
           "follower teardown can remove the primary before its release");

    expect(Hyprexpo::validOverviewSize(Size{1280, 720}, Size{2560, 1440}), "scaled monitor size is valid");
    expect(Hyprexpo::validOverviewSize(Size{1080, 1920}, Size{1920, 1080}), "rotated monitor size is valid");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto invalid : {Size{0, 720}, Size{1280, 0}, Size{-1, 720}, Size{1280, -1}, Size{nan, 720}, Size{1280, nan}, Size{inf, 720}, Size{1280, inf}}) {
        expect(!Hyprexpo::validOverviewSize(invalid, Size{1280, 720}), "invalid logical geometry is rejected");
        expect(!Hyprexpo::validOverviewSize(Size{1280, 720}, invalid), "invalid pixel geometry is rejected");
    }
    if (failures)
        return EXIT_FAILURE;
    std::cout << "OverviewSwipeTests passed (ASan + UBSan)\n";
}
