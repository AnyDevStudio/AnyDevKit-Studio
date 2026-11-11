#ifndef Lan_FakeNet_hpp
#define Lan_FakeNet_hpp
#include "Online/Session.hpp"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <random>
#include <vector>

// A network in memory for the session tests: a clock the test moves, and datagrams that arrive after a
// random delay (so they overtake each other), get lost or arrive twice, as configured.
namespace FakeNet {

struct Packet {
    std::uint64_t DeliverAt = 0;
    std::uint64_t Order = 0;
    std::uint32_t From = 0;
    std::uint16_t FromPort = 0;
    std::uint32_t To = 0;
    std::uint16_t ToPort = 0;
    std::vector<std::uint8_t> Bytes;
};

class Endpoint;

class Network {
public:
    explicit Network(std::uint64_t seed = 1) : random(seed) {}

    double Loss = 0.0;      // chance a datagram never arrives
    double Duplicate = 0.0; // chance it arrives a second time
    std::uint64_t MinDelayMs = 1;
    std::uint64_t MaxDelayMs = 1;
    std::uint64_t NowMs = 1000;
    std::uint64_t Sent = 0;
    std::uint64_t Lost = 0;
    std::function<void(const Packet&)> Tap; // sees every datagram as sent

    void Attach(Endpoint* endpoint) { endpoints.push_back(endpoint); }

    void Send(std::uint32_t from, std::uint16_t fromPort, std::uint32_t to, std::uint16_t toPort, std::span<const std::uint8_t> bytes) {
        ++Sent;
        Packet packet{0, 0, from, fromPort, to, toPort, {bytes.begin(), bytes.end()}};
        if (Tap) Tap(packet);
        if (chance(Loss)) {
            ++Lost;
            return;
        }
        schedule(packet);
        if (chance(Duplicate)) schedule(packet);
    }

    // A datagram put straight on the wire, with no loss or delay: for crafted and damaged packets.
    void Inject(std::uint32_t from, std::uint16_t fromPort, std::uint32_t to, std::uint16_t toPort, std::span<const std::uint8_t> bytes) {
        inflight.push_back({NowMs, order++, from, fromPort, to, toPort, {bytes.begin(), bytes.end()}});
    }

    void Deliver();

    std::uint64_t Random(std::uint64_t low, std::uint64_t high) { return std::uniform_int_distribution<std::uint64_t>(low, high)(random); }

private:
    bool chance(double probability) { return probability > 0 && std::uniform_real_distribution<double>(0, 1)(random) < probability; }
    void schedule(Packet packet) {
        packet.DeliverAt = NowMs + Random(MinDelayMs, MaxDelayMs);
        packet.Order = order++;
        inflight.push_back(std::move(packet));
    }

    std::mt19937_64 random;
    std::uint64_t order = 0;
    std::vector<Packet> inflight;
    std::vector<Endpoint*> endpoints;
};

class Endpoint final : public Lan::Transport {
public:
    Endpoint(Network& network, std::uint32_t address, std::uint16_t port) : network(network), Address(address), Port(port) { network.Attach(this); }

    bool SendTo(std::uint32_t address, std::uint16_t port, std::span<const std::uint8_t> bytes) override {
        if (bytes.size() > Lan::MaxDatagramBytes) ++Oversized;
        network.Send(Address, Port, address, port, bytes);
        return true;
    }
    bool Broadcast(std::uint16_t port, std::span<const std::uint8_t> bytes) override { return SendTo(Lan::LimitedBroadcast, port, bytes); }
    std::optional<Lan::Received> Receive(std::span<std::uint8_t> buffer) override {
        if (Inbox.empty()) return std::nullopt;
        const Packet packet = std::move(Inbox.front());
        Inbox.pop_front();
        const auto size = std::min(buffer.size(), packet.Bytes.size());
        std::copy_n(packet.Bytes.begin(), size, buffer.begin());
        return Lan::Received{packet.From, packet.FromPort, size};
    }

    Network& network;
    std::uint32_t Address;
    std::uint16_t Port;
    std::deque<Packet> Inbox;
    std::uint64_t Oversized = 0;
};

inline void Network::Deliver() {
    std::stable_sort(inflight.begin(), inflight.end(), [](const Packet& a, const Packet& b) { return a.DeliverAt != b.DeliverAt ? a.DeliverAt < b.DeliverAt : a.Order < b.Order; });
    std::size_t delivered = 0;
    for (; delivered < inflight.size() && inflight[delivered].DeliverAt <= NowMs; ++delivered) {
        const auto& packet = inflight[delivered];
        for (auto* endpoint : endpoints) {
            const bool addressed = packet.To == Lan::LimitedBroadcast || packet.To == endpoint->Address;
            if (addressed && packet.ToPort == endpoint->Port) endpoint->Inbox.push_back(packet);
        }
    }
    inflight.erase(inflight.begin(), inflight.begin() + static_cast<std::ptrdiff_t>(delivered));
}

}
#endif
