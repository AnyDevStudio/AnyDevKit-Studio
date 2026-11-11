// Session protocol tests on the in-memory network: discovery, joining, reliable events under loss,
// reordering and duplicates, the newest-wins state channel and its fragments, timeouts, and damaged input.
#include "Online/Session.hpp"
#include "support/FakeNet.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;
#define CHECK(condition)                                                                   \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            ++failures;                                                                    \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);             \
        }                                                                                  \
    } while (0)

using Kind = Lan::Notice::Kind;
constexpr std::uint32_t GameId = 0xA11CE;
constexpr std::uint32_t Build = 7;
constexpr std::uint32_t HostAddress = Lan::Ipv4(10, 0, 0, 1);

struct Got {
    Kind Type;
    int Slot;
    Lan::Refusal Reason;
    std::vector<std::uint8_t> Bytes;
    std::uint64_t AtMs;
};

struct Node {
    Node(FakeNet::Network& network, std::uint32_t address, std::uint16_t port) : endpoint(network, address, port), session(std::make_unique<Lan::Session>(endpoint)) {}
    FakeNet::Endpoint endpoint;
    std::unique_ptr<Lan::Session> session;
    bool Active = true; // false: the node stops calling Update, as a crashed or frozen game would
    bool Poll = true;
    std::vector<Got> Notices;

    Lan::Session& S() { return *session; }
    std::size_t Count(Kind type) const {
        std::size_t count = 0;
        for (const auto& notice : Notices) count += notice.Type == type;
        return count;
    }
    const Got* Last(Kind type) const {
        for (auto it = Notices.rbegin(); it != Notices.rend(); ++it) {
            if (it->Type == type) return &*it;
        }
        return nullptr;
    }
    std::vector<std::vector<std::uint8_t>> Events() const {
        std::vector<std::vector<std::uint8_t>> events;
        for (const auto& notice : Notices) {
            if (notice.Type == Kind::Event) events.push_back(notice.Bytes);
        }
        return events;
    }
};

struct World {
    explicit World(std::uint64_t seed = 1) : network(seed) {}
    FakeNet::Network network;
    std::vector<std::unique_ptr<Node>> nodes;

    Node& Add(std::uint32_t address, std::uint16_t port) {
        nodes.push_back(std::make_unique<Node>(network, address, port));
        return *nodes.back();
    }
    void Step(std::uint64_t ms) {
        network.NowMs += ms;
        network.Deliver();
        for (auto& node : nodes) {
            if (!node->Active) continue;
            node->S().Update(network.NowMs);
            if (!node->Poll) continue;
            while (const auto* notice = node->S().PollNotice()) {
                node->Notices.push_back({notice->Type, notice->Slot, notice->Reason, {notice->Bytes().begin(), notice->Bytes().end()}, network.NowMs});
            }
        }
    }
    void Run(std::uint64_t ms, std::uint64_t step = 5) {
        for (std::uint64_t elapsed = 0; elapsed < ms; elapsed += step) Step(step);
    }
    template <typename Done>
    bool RunUntil(Done done, std::uint64_t limitMs, std::uint64_t step = 5) {
        for (std::uint64_t elapsed = 0; elapsed < limitMs; elapsed += step) {
            if (done()) return true;
            Step(step);
        }
        return done();
    }
};

Lan::Config Settings(std::string_view name, std::uint64_t id, std::uint32_t build = Build, std::uint8_t maxPlayers = 4) {
    Lan::Config config;
    config.GameId = GameId;
    config.GameBuild = build;
    config.LocalName = name;
    config.LocalId = id;
    config.MaxPlayers = maxPlayers;
    return config;
}

Node& AddGuest(World& world, int number) { return world.Add(Lan::Ipv4(10, 0, 0, static_cast<std::uint8_t>(10 + number)), Lan::DefaultDiscoveryPort); }

bool Connect(World& world, Node& host, Node& guest, std::uint64_t guestId) {
    if (host.S().CurrentRole() != Lan::Session::Role::Host) host.S().Host(Settings("Host", 1));
    guest.S().Browse(Settings("Guest", guestId));
    if (!world.RunUntil([&] { return !guest.S().Hosts().empty(); }, 5000)) return false;
    const auto entry = guest.S().Hosts()[0];
    guest.S().Join(entry.Address, entry.Port);
    return world.RunUntil([&] { return guest.S().CurrentRole() == Lan::Session::Role::Guest; }, 10000);
}

std::vector<std::uint8_t> EventBytes(std::uint32_t index) {
    std::vector<std::uint8_t> bytes(4 + (index * 37u) % (Lan::MaxEventBytes - 3));
    std::memcpy(bytes.data(), &index, 4);
    for (std::size_t at = 4; at < bytes.size(); ++at) bytes[at] = static_cast<std::uint8_t>(index * 13u + at);
    return bytes;
}

std::vector<std::uint8_t> StateBytes(std::uint32_t index, std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t at = 0; at < size; ++at) bytes[at] = static_cast<std::uint8_t>(index * 31u + at * 7u);
    if (size >= 4) std::memcpy(bytes.data(), &index, 4);
    return bytes;
}

std::uint32_t IndexOf(std::span<const std::uint8_t> bytes) {
    std::uint32_t index = 0;
    if (bytes.size() >= 4) std::memcpy(&index, bytes.data(), 4);
    return index;
}

void TestUtf8Names() {
    std::printf("utf8 names\n");
    CHECK(Lan::Utf8Prefix("abc", 16) == 3);
    CHECK(Lan::Utf8Prefix("abcdefghijklmnopqrstuvwxyz", 16) == 16);
    CHECK(Lan::Utf8Prefix("abcdefghijklmno\xc3\xa9", 16) == 15);                 // é would end at byte 17
    CHECK(Lan::Utf8Prefix("\xc4\x9f\xc4\x9f\xc4\x9f\xc4\x9f\xc4\x9f\xc4\x9f\xc4\x9f\xc4\x9f\xc4\x9f", 16) == 16); // nine ğ
    CHECK(Lan::Utf8Prefix("abcdefghijklmn\xf0\x9f\x8e\xae", 16) == 14);           // a 4-byte emoji at 14
    Lan::Name name;
    name.Set("Çağrı'nın Oyunu Uzun");
    CHECK(name.Size <= Lan::MaxNameBytes);
    CHECK(name.View() == std::string_view("Çağrı'nın Oy").substr(0, name.Size));
}

void TestDiscovery() {
    std::printf("discovery: listed within 1 s, other games and builds ignored, gone after 3 s\n");
    World world;
    Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
    Node& otherGame = world.Add(Lan::Ipv4(10, 0, 0, 2), Lan::DefaultGamePort);
    Node& otherBuild = world.Add(Lan::Ipv4(10, 0, 0, 3), Lan::DefaultGamePort);
    Node& guest = AddGuest(world, 1);
    host.S().Host(Settings("Ayşe'nin Odası", 1));
    auto other = Settings("Other", 2);
    other.GameId = GameId + 1;
    otherGame.S().Host(other);
    otherBuild.S().Host(Settings("Old build", 3, Build + 1));
    guest.S().Browse(Settings("Guest", 9));
    world.Run(1100);
    CHECK(guest.S().Hosts().size() == 1);
    if (!guest.S().Hosts().empty()) {
        const auto& entry = guest.S().Hosts()[0];
        CHECK(entry.Address == HostAddress);
        CHECK(entry.Port == Lan::DefaultGamePort);
        CHECK(entry.HostName.View() == "Ayşe'nin Odası");
        CHECK(entry.Players == 1);
        CHECK(entry.MaxPlayers == 4);
    }
    const auto sentBefore = host.S().Statistics().DatagramsSent;
    world.Run(5000);
    const auto announces = host.S().Statistics().DatagramsSent - sentBefore;
    std::printf("  announces in 5 s: %llu\n", static_cast<unsigned long long>(announces));
    CHECK(announces >= 4 && announces <= 6);
    host.Active = false;
    const auto lastSeen = guest.S().Hosts().empty() ? 0 : guest.S().Hosts()[0].LastSeenMs;
    world.RunUntil([&] { return guest.S().Hosts().empty(); }, 10000);
    const auto gone = world.network.NowMs - lastSeen;
    std::printf("  host dropped from the list %llu ms after its last announce\n", static_cast<unsigned long long>(gone));
    CHECK(gone > Lan::HostLifetimeMs && gone <= Lan::HostLifetimeMs + 5);
}

void TestJoinSlotsAndRefusals() {
    std::printf("join: slots 1 to 3, a fifth player refused as full, another build refused as version\n");
    World world;
    Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
    host.S().Host(Settings("Host", 1));
    std::vector<Node*> guests;
    for (int number = 1; number <= 3; ++number) {
        guests.push_back(&AddGuest(world, number));
        CHECK(Connect(world, host, *guests.back(), 100 + static_cast<std::uint64_t>(number)));
        CHECK(guests.back()->S().LocalSlot() == number);
        CHECK(guests.back()->Count(Kind::Joined) == 1);
    }
    CHECK(host.Count(Kind::Joined) == 3);
    CHECK(host.S().PeerCount() == 3);
    world.Run(1100);
    CHECK(!guests[0]->S().Hosts().empty() && guests[0]->S().Hosts()[0].Players == 4);

    Node& late = AddGuest(world, 4);
    late.S().Browse(Settings("Late", 200));
    world.RunUntil([&] { return !late.S().Hosts().empty(); }, 3000);
    late.S().Join(HostAddress, Lan::DefaultGamePort);
    world.RunUntil([&] { return late.Count(Kind::Refused) > 0; }, 3000);
    CHECK(late.Count(Kind::Refused) == 1 && late.Last(Kind::Refused)->Reason == Lan::Refusal::Full);
    CHECK(late.S().CurrentRole() == Lan::Session::Role::Browsing);

    Node& stale = AddGuest(world, 5);
    stale.S().Browse(Settings("Stale", 201, Build + 1));
    stale.S().Join(HostAddress, Lan::DefaultGamePort); // its list stays empty: the host's build differs
    world.RunUntil([&] { return stale.Count(Kind::Refused) > 0; }, 3000);
    CHECK(stale.S().Hosts().empty());
    CHECK(stale.Count(Kind::Refused) == 1 && stale.Last(Kind::Refused)->Reason == Lan::Refusal::Version);

    // A Join from a future protocol version gets Refuse(Version), with no guest id.
    FakeNet::Endpoint raw(world.network, Lan::Ipv4(10, 0, 0, 99), 5000);
    const std::uint8_t future[] = {'A', 'D', 'K', 'L', 2, 2, 1, 2, 3};
    world.network.Inject(raw.Address, raw.Port, HostAddress, Lan::DefaultGamePort, future);
    world.Step(5);
    world.Step(5);
    bool refused = false;
    for (const auto& packet : raw.Inbox) {
        refused |= packet.Bytes.size() == 19 && packet.Bytes[4] == 1 && packet.Bytes[5] == 4 && packet.Bytes[18] == 2;
    }
    CHECK(refused);
    CHECK(host.S().PeerCount() == 3);
}

// 200 events each way over a network that loses 20%, reorders (5 to 60 ms of delay) and duplicates 5%.
void TestReliableEventsUnderLoss(std::uint64_t seed) {
    std::printf("reliable events, 20%% loss + reordering + 5%% duplicates, seed %llu\n", static_cast<unsigned long long>(seed));
    World world(seed);
    Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
    Node& guest = AddGuest(world, 1);
    CHECK(Connect(world, host, guest, 42));
    world.network.Loss = 0.2;
    world.network.Duplicate = 0.05;
    world.network.MinDelayMs = 5;
    world.network.MaxDelayMs = 60;
    constexpr std::uint32_t Count = 200;
    std::uint32_t hostSent = 0;
    std::uint32_t guestSent = 0;
    std::uint64_t windowFull = 0;
    const bool done = world.RunUntil([&] {
        while (hostSent < Count && host.S().SendEventTo(1, EventBytes(hostSent + 1))) ++hostSent;
        while (guestSent < Count && guest.S().SendEvent(EventBytes(100000 + guestSent + 1))) ++guestSent;
        windowFull += hostSent < Count;
        return guest.Count(Kind::Event) == Count && host.Count(Kind::Event) == Count;
    }, 300000);
    CHECK(done);
    world.Run(3000); // nothing more may arrive: no duplicate deliveries
    const auto toGuest = guest.Events();
    const auto toHost = host.Events();
    CHECK(toGuest.size() == Count);
    CHECK(toHost.size() == Count);
    bool ordered = true;
    for (std::uint32_t index = 0; index < toGuest.size(); ++index) ordered &= toGuest[index] == EventBytes(index + 1);
    for (std::uint32_t index = 0; index < toHost.size(); ++index) ordered &= toHost[index] == EventBytes(100000 + index + 1);
    CHECK(ordered);
    for (const auto& notice : guest.Notices) CHECK(notice.Type != Kind::Event || notice.Slot == 0);
    for (const auto& notice : host.Notices) CHECK(notice.Type != Kind::Event || notice.Slot == 1);
    CHECK(host.Count(Kind::Left) == 0 && guest.Count(Kind::Left) == 0);
    CHECK(host.S().Peer(1)->PendingEvents == 0 && guest.S().Peer(0)->PendingEvents == 0);
    CHECK(host.S().Statistics().EventResends > 0);
    std::printf("  sent %llu datagrams, lost %llu; host resends %llu, guest resends %llu, window-full frames %llu\n",
        static_cast<unsigned long long>(world.network.Sent), static_cast<unsigned long long>(world.network.Lost),
        static_cast<unsigned long long>(host.S().Statistics().EventResends), static_cast<unsigned long long>(guest.S().Statistics().EventResends),
        static_cast<unsigned long long>(windowFull));
}

// The game stops reading notices: events wait in the window, the sender resends, nothing is lost.
void TestEventsWaitForTheGame() {
    std::printf("reliable events wait when the game does not poll notices\n");
    World world;
    Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
    Node& guest = AddGuest(world, 1);
    CHECK(Connect(world, host, guest, 42));
    guest.Poll = false;
    std::uint32_t sent = 0;
    world.RunUntil([&] {
        while (sent < 150 && host.S().SendEventTo(1, EventBytes(sent + 1))) ++sent;
        return false;
    }, 2000);
    CHECK(sent < 150); // the window filled
    CHECK(host.S().Peer(1)->PendingEvents == Lan::EventWindow);
    guest.Poll = true;
    world.RunUntil([&] {
        while (sent < 150 && host.S().SendEventTo(1, EventBytes(sent + 1))) ++sent;
        return guest.Count(Kind::Event) == 150;
    }, 20000);
    const auto events = guest.Events();
    CHECK(events.size() == 150);
    bool ordered = events.size() == 150;
    for (std::uint32_t index = 0; ordered && index < events.size(); ++index) ordered = events[index] == EventBytes(index + 1);
    CHECK(ordered);
    CHECK(guest.S().Statistics().NoticesDropped == 0);
    CHECK(!host.S().SendEventTo(1, std::vector<std::uint8_t>(Lan::MaxEventBytes + 1)));
    CHECK(!host.S().SendEventTo(2, EventBytes(1))); // nobody in slot 2
}

struct StateRun {
    std::uint32_t Received = 0;
    std::uint32_t Last = 0;
    bool Increasing = true;
    bool Intact = true;
};

StateRun SendStates(World& world, Node& host, Node& guest, std::uint32_t count, std::size_t size, std::uint64_t everyMs) {
    StateRun run;
    auto take = [&] {
        while (const auto state = guest.S().TakeState(0)) {
            const auto index = IndexOf(state->Bytes);
            run.Increasing &= index > run.Last;
            run.Intact &= state->Bytes.size() == size && std::equal(state->Bytes.begin(), state->Bytes.end(), StateBytes(index, size).begin());
            run.Last = index;
            ++run.Received;
        }
    };
    for (std::uint32_t index = 1; index <= count; ++index) {
        CHECK(host.S().SendStateTo(1, StateBytes(index, size)));
        world.Step(everyMs);
        take();
    }
    for (int drain = 0; drain < 100; ++drain) {
        world.Step(5);
        take();
    }
    return run;
}

void TestStateNewestWins() {
    std::printf("state: 8000-byte snapshots (7 fragments) at 60 Hz, every datagram delayed 1 to 20 ms\n");
    World world(3);
    Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
    Node& guest = AddGuest(world, 1);
    CHECK(Connect(world, host, guest, 42));
    world.network.MinDelayMs = 1;
    world.network.MaxDelayMs = 20;
    const auto reordered = SendStates(world, host, guest, 300, 8000, 16);
    std::printf("  received %u of 300, newest %u\n", reordered.Received, reordered.Last);
    CHECK(reordered.Increasing && reordered.Intact);
    CHECK(reordered.Last == 300); // nothing is lost, so the last one sent is the one kept
    CHECK(reordered.Received >= 200); // the rest finished after a newer one had: dropped, by design
    CHECK(!guest.S().TakeState(0)); // taken once only

    std::printf("state: the same with 20%% loss and 5%% duplicates\n");
    world.network.Loss = 0.2;
    world.network.Duplicate = 0.05;
    const auto lossy = SendStates(world, host, guest, 300, 8000, 16);
    std::printf("  received %u of 300 (a state needs all 7 fragments: 0.8^7 = 21%%), newest %u\n", lossy.Received, lossy.Last);
    CHECK(lossy.Increasing && lossy.Intact);
    CHECK(lossy.Received >= 30);

    std::printf("state: heavy reordering: sent every 5 ms, every datagram delayed 1 to 30 ms, no loss\n");
    world.network.Loss = 0;
    world.network.Duplicate = 0;
    world.network.MaxDelayMs = 30;
    const auto stale = guest.S().Statistics().StaleStateFragments;
    const auto jumbled = SendStates(world, host, guest, 300, 8000, 5);
    std::printf("  received %u of 300, newest %u, stale fragments dropped %llu\n", jumbled.Received, jumbled.Last,
        static_cast<unsigned long long>(guest.S().Statistics().StaleStateFragments - stale));
    CHECK(jumbled.Increasing && jumbled.Intact);
    CHECK(jumbled.Last == 300);
    CHECK(jumbled.Received >= 30);
    CHECK(guest.S().Statistics().StaleStateFragments > stale);
    world.network.MinDelayMs = world.network.MaxDelayMs = 1;

    std::printf("state: largest size, empty, too large; guest to host\n");
    world.network.Loss = 0;
    world.network.Duplicate = 0;
    CHECK(host.S().SendStateTo(1, StateBytes(77, Lan::MaxStateBytes)));
    world.Run(100);
    auto state = guest.S().TakeState(0);
    CHECK(state && state->Bytes.size() == Lan::MaxStateBytes && IndexOf(state->Bytes) == 77);
    CHECK(state && std::equal(state->Bytes.begin(), state->Bytes.end(), StateBytes(77, Lan::MaxStateBytes).begin()));
    CHECK(host.S().SendState({}));
    world.Run(100);
    state = guest.S().TakeState(0);
    CHECK(state && state->Bytes.empty());
    CHECK(!host.S().SendState(std::vector<std::uint8_t>(Lan::MaxStateBytes + 1)));
    CHECK(guest.S().SendState(StateBytes(5, 300)));
    world.Run(100);
    state = host.S().TakeState(1);
    CHECK(state && IndexOf(state->Bytes) == 5 && state->Bytes.size() == 300);
    CHECK(host.endpoint.Oversized == 0 && guest.endpoint.Oversized == 0);
}

// A crafted old fragment after a newer complete state is dropped; a fragment set with a hole never completes.
void TestStaleAndPartialStates() {
    std::printf("state: stale and incomplete fragments\n");
    World world;
    Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
    Node& guest = AddGuest(world, 1);
    CHECK(Connect(world, host, guest, 42));
    auto fragment = [&](std::uint32_t sequence, std::uint8_t index, std::uint8_t count, std::uint16_t total, std::size_t size) {
        std::vector<std::uint8_t> packet{'A', 'D', 'K', 'L', Lan::ProtocolVersion, 8};
        for (int shift = 0; shift < 32; shift += 8) packet.push_back(static_cast<std::uint8_t>(sequence >> shift));
        packet.push_back(index);
        packet.push_back(count);
        packet.push_back(static_cast<std::uint8_t>(total));
        packet.push_back(static_cast<std::uint8_t>(total >> 8));
        packet.resize(packet.size() + size, static_cast<std::uint8_t>(sequence));
        world.network.Inject(HostAddress, Lan::DefaultGamePort, guest.endpoint.Address, guest.endpoint.Port, packet);
        world.Step(1);
    };
    fragment(50, 0, 1, 10, 10);
    auto state = guest.S().TakeState(0);
    CHECK(state && state->Sequence == 50);
    fragment(49, 0, 1, 10, 10);
    CHECK(!guest.S().TakeState(0));
    fragment(60, 0, 2, 1300, Lan::StateFragmentBytes); // second half never comes
    fragment(61, 1, 2, 1300, 1300 - Lan::StateFragmentBytes);
    fragment(61, 0, 2, 1300, Lan::StateFragmentBytes);
    state = guest.S().TakeState(0);
    CHECK(state && state->Sequence == 61 && state->Bytes.size() == 1300);
    fragment(60, 1, 2, 1300, 1300 - Lan::StateFragmentBytes); // too late: 61 is newer
    CHECK(!guest.S().TakeState(0));
    const auto dropped = guest.S().Statistics().DatagramsDropped;
    fragment(70, 0, 1, 10, 9);   // wrong size
    fragment(71, 1, 1, 10, 10);  // index past count
    fragment(72, 0, 3, 1300, Lan::StateFragmentBytes); // count does not match total
    fragment(0, 0, 1, 10, 10);   // sequence 0
    CHECK(guest.S().Statistics().DatagramsDropped == dropped + 4);
    CHECK(!guest.S().TakeState(0));
}

void TestTimeouts() {
    std::printf("timeouts: silent host, silent guest, unanswered join\n");
    {
        World world;
        Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
        Node& guest = AddGuest(world, 1);
        CHECK(Connect(world, host, guest, 42));
        world.Run(1000);
        host.Active = false;
        const auto stopped = world.network.NowMs;
        world.RunUntil([&] { return guest.Count(Kind::Left) > 0; }, 10000);
        CHECK(guest.Count(Kind::Left) == 1);
        const auto after = guest.Last(Kind::Left) ? guest.Last(Kind::Left)->AtMs - stopped : 0;
        std::printf("  guest saw the host leave %llu ms after it went silent\n", static_cast<unsigned long long>(after));
        CHECK(after >= 4700 && after <= 5300);
        CHECK(guest.S().CurrentRole() == Lan::Session::Role::Browsing);
        CHECK(!guest.S().Peer(0));
    }
    {
        World world;
        Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
        Node& guest = AddGuest(world, 1);
        CHECK(Connect(world, host, guest, 42));
        world.Run(4000); // pings keep it alive well past 5 s since joining
        CHECK(host.Count(Kind::Left) == 0 && host.S().PeerCount() == 1);
        world.Run(2000);
        CHECK(host.Count(Kind::Left) == 0);
        guest.Active = false;
        const auto stopped = world.network.NowMs;
        world.RunUntil([&] { return host.Count(Kind::Left) > 0; }, 10000);
        const auto* left = host.Last(Kind::Left);
        CHECK(left && left->Slot == 1);
        const auto after = left ? left->AtMs - stopped : 0;
        std::printf("  host saw the guest leave %llu ms after it went silent\n", static_cast<unsigned long long>(after));
        CHECK(after >= 4700 && after <= 5300);
        CHECK(host.S().PeerCount() == 0);
    }
    {
        World world;
        Node& guest = AddGuest(world, 1);
        guest.S().Browse(Settings("Guest", 42));
        world.Step(5);
        const auto started = world.network.NowMs;
        guest.S().Join(Lan::Ipv4(10, 0, 0, 200), Lan::DefaultGamePort);
        world.RunUntil([&] { return guest.Count(Kind::JoinTimedOut) > 0; }, 10000);
        CHECK(guest.Count(Kind::JoinTimedOut) == 1);
        const auto after = guest.Last(Kind::JoinTimedOut) ? guest.Last(Kind::JoinTimedOut)->AtMs - started : 0;
        CHECK(after >= 4990 && after <= 5100);
        CHECK(guest.S().CurrentRole() == Lan::Session::Role::Browsing);
    }
}

void TestRoundTrip() {
    std::printf("round trip: 20 ms each way\n");
    World world;
    world.network.MinDelayMs = world.network.MaxDelayMs = 20;
    Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
    Node& guest = AddGuest(world, 1);
    CHECK(Connect(world, host, guest, 42));
    world.Run(3000, 1);
    const auto fromHost = host.S().Peer(1);
    const auto fromGuest = guest.S().Peer(0);
    CHECK(fromHost && fromGuest);
    if (fromHost && fromGuest) {
        std::printf("  host measures %u ms (last %u), guest measures %u ms (last %u)\n", fromHost->RttMs, fromHost->LastRttMs, fromGuest->RttMs, fromGuest->LastRttMs);
        CHECK(fromHost->RttMs >= 40 && fromHost->RttMs <= 42);
        CHECK(fromGuest->RttMs >= 40 && fromGuest->RttMs <= 42);
        CHECK(fromHost->PeerName.View() == "Guest" && fromGuest->PeerName.View() == "Host");
    }
}

void TestLeaveAndRejoin() {
    std::printf("leave, and a guest that starts over\n");
    World world;
    Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
    Node& guest = AddGuest(world, 1);
    CHECK(Connect(world, host, guest, 42));
    guest.S().Leave();
    world.Run(20);
    CHECK(host.Count(Kind::Left) == 1 && host.S().PeerCount() == 0);
    CHECK(guest.S().CurrentRole() == Lan::Session::Role::Idle);

    CHECK(Connect(world, host, guest, 42));
    for (std::uint32_t index = 1; index <= 5; ++index) CHECK(host.S().SendEventTo(1, EventBytes(index)));
    world.Run(200);
    CHECK(guest.Count(Kind::Event) == 5);
    // The guest's game restarts with the same id and joins again; the host had not noticed it go.
    guest.session = std::make_unique<Lan::Session>(guest.endpoint);
    guest.Notices.clear();
    host.Notices.clear();
    CHECK(Connect(world, host, guest, 42));
    CHECK(host.Count(Kind::Left) == 1 && host.Count(Kind::Joined) == 1 && host.S().PeerCount() == 1);
    for (std::uint32_t index = 1; index <= 5; ++index) CHECK(host.S().SendEventTo(1, EventBytes(index)));
    world.Run(200);
    CHECK(guest.Count(Kind::Event) == 5); // a fresh connection starts its events at 1 again
}

// Damaged copies of real traffic and random bytes, from the peer's own address and from strangers.
void TestDamagedInput() {
    std::printf("damaged input: mutated and truncated packets, random bytes\n");
    World world(11);
    Node& host = world.Add(HostAddress, Lan::DefaultGamePort);
    Node& guest = AddGuest(world, 1);
    std::vector<FakeNet::Packet> captured;
    world.network.Tap = [&](const FakeNet::Packet& packet) {
        if (captured.size() < 400) captured.push_back(packet);
    };
    CHECK(Connect(world, host, guest, 42));
    for (std::uint32_t index = 1; index <= 20; ++index) host.S().SendEventTo(1, EventBytes(index));
    host.S().SendStateTo(1, StateBytes(1, 5000));
    world.Run(500);
    world.network.Tap = nullptr;
    CHECK(captured.size() > 20);
    for (int round = 0; round < 20000; ++round) {
        auto packet = captured[world.network.Random(0, captured.size() - 1)];
        switch (world.network.Random(0, 3)) {
            case 0: packet.Bytes.resize(world.network.Random(0, packet.Bytes.size())); break;
            case 1: packet.Bytes[world.network.Random(0, packet.Bytes.size() - 1)] ^= static_cast<std::uint8_t>(world.network.Random(1, 255)); break;
            case 2: packet.Bytes.assign(world.network.Random(0, 1300), static_cast<std::uint8_t>(world.network.Random(0, 255))); break;
            default:
                for (int flips = 0; flips < 4; ++flips) packet.Bytes[world.network.Random(0, packet.Bytes.size() - 1)] = static_cast<std::uint8_t>(world.network.Random(0, 255));
        }
        const bool stranger = world.network.Random(0, 4) == 0;
        world.network.Inject(stranger ? Lan::Ipv4(10, 9, 9, 9) : packet.From, packet.FromPort, packet.To, packet.ToPort, packet.Bytes);
        if (round % 50 == 0) world.Step(1);
    }
    world.Run(500);
    // The session survives and still works: fresh events and states go through.
    host.Notices.clear();
    guest.Notices.clear();
    // Forged packets are possible on a LAN (nothing is signed): a damaged Join can add a phantom guest or
    // restart ours, a damaged Leave can end the session. Only crashes and stuck states are failures here.
    const auto view = host.S().Peer(guest.S().LocalSlot());
    const bool connected = guest.S().CurrentRole() == Lan::Session::Role::Guest && view && view->Address == guest.endpoint.Address && view->Port == guest.endpoint.Port;
    std::printf("  same connection after 20000 damaged packets: %s; host peers %d (phantoms come from damaged Joins)\n", connected ? "yes" : "no", host.S().PeerCount());
    if (!connected) {
        host.S().Host(Settings("Host", 1)); // start a clean session; the test is that nothing crashed or wedged
        CHECK(Connect(world, host, guest, 43));
        host.Notices.clear();
    }
    const auto guestView = guest.S().Peer(0);
    CHECK(guestView && guestView->PendingEvents == 0);
    std::uint32_t sent = 0;
    world.RunUntil([&] {
        while (sent < 10 && guest.S().SendEvent(EventBytes(500 + sent))) ++sent;
        return host.Count(Kind::Event) >= 10;
    }, 5000);
    CHECK(host.Count(Kind::Event) == 10);
    CHECK(host.S().SendStateTo(guest.S().LocalSlot(), StateBytes(999999, 3000)));
    world.Run(100);
    const auto state = guest.S().TakeState(0);
    CHECK(state && state->Bytes.size() == 3000);
}

}

int main() {
    std::printf("sizeof(Lan::Session) = %zu bytes\n", sizeof(Lan::Session));
    TestUtf8Names();
    TestDiscovery();
    TestJoinSlotsAndRefusals();
    for (std::uint64_t seed = 1; seed <= 5; ++seed) TestReliableEventsUnderLoss(seed);
    TestEventsWaitForTheGame();
    TestStateNewestWins();
    TestStaleAndPartialStates();
    TestTimeouts();
    TestRoundTrip();
    TestLeaveAndRejoin();
    TestDamagedInput();
    if (failures) {
        std::printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("all session tests passed\n");
    return 0;
}
