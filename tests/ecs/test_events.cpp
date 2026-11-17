#include "ge/ecs/events.hpp"

#include "../support/mini_test.hpp"

#include <vector>

using ge::ecs::EventCursor;
using ge::ecs::Events;

namespace {

struct Ping {
    int id = 0;
};

std::vector<int> read_ids(const Events<Ping>& events, EventCursor& cursor) {
    std::vector<int> ids;
    events.read(cursor, [&ids](const Ping& ping) { ids.push_back(ping.id); });
    return ids;
}

} // namespace

TEST_CASE("events: a reader sees each event once") {
    Events<Ping> events;
    EventCursor cursor;
    events.send(Ping{1});
    events.send(Ping{2});
    CHECK(read_ids(events, cursor) == (std::vector<int>{1, 2}));
    CHECK(read_ids(events, cursor).empty());
    events.send(Ping{3});
    CHECK(read_ids(events, cursor) == (std::vector<int>{3}));
}

TEST_CASE("events: readers are independent") {
    Events<Ping> events;
    EventCursor a;
    EventCursor b;
    events.send(Ping{1});
    CHECK(read_ids(events, a) == (std::vector<int>{1}));
    events.send(Ping{2});
    CHECK(read_ids(events, b) == (std::vector<int>{1, 2}));
    CHECK(read_ids(events, a) == (std::vector<int>{2}));
}

TEST_CASE("events: emplace constructs in place") {
    Events<Ping> events;
    const Ping& ping = events.emplace(7);
    CHECK(ping.id == 7);
    CHECK(events.size() == 1U);
    CHECK(events.total_sent() == 1U);
}

TEST_CASE("events: events survive one update, not two") {
    Events<Ping> events;
    EventCursor fresh;
    events.send(Ping{1});
    events.update();
    events.send(Ping{2});
    CHECK(events.size() == 2U);
    CHECK(read_ids(events, fresh) == (std::vector<int>{1, 2}));
    events.update();
    CHECK(events.size() == 1U);
    events.update();
    CHECK(events.size() == 0U);
    CHECK(events.total_sent() == 2U);
}

TEST_CASE("events: a lagging reader skips dropped events") {
    Events<Ping> events;
    EventCursor lagging;
    events.send(Ping{1});
    events.update();
    events.update();
    events.send(Ping{2});
    CHECK(read_ids(events, lagging) == (std::vector<int>{2}));
}

TEST_CASE("events: for_each visits old then new, clear empties") {
    Events<Ping> events;
    events.send(Ping{1});
    events.update();
    events.send(Ping{2});
    std::vector<int> ids;
    events.for_each([&ids](const Ping& ping) { ids.push_back(ping.id); });
    CHECK(ids == (std::vector<int>{1, 2}));
    EventCursor cursor;
    events.clear();
    CHECK(events.size() == 0U);
    CHECK(read_ids(events, cursor).empty());
    events.send(Ping{3});
    CHECK(read_ids(events, cursor) == (std::vector<int>{3}));
}
