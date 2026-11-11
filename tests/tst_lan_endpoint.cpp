// Lan::Endpoint on BSD sockets, on this machine: binding, non-blocking receive, loopback and broadcast.
#include "Online/Lan.hpp"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

int failures = 0;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ++failures;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                      \
    } while (0)

std::optional<Lan::Received> waitFor(Lan::Endpoint& endpoint, std::span<std::uint8_t> buffer) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (auto received = endpoint.Receive(buffer)) return received;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return std::nullopt;
}

std::string text(std::uint32_t address) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", address >> 24, (address >> 16) & 255, (address >> 8) & 255, address & 255);
    return buffer;
}

}

int main() {
    std::uint8_t buffer[1500];
    Lan::Endpoint receiver;
    Lan::Endpoint sender;
    std::printf("open on a free port\n");
    CHECK(receiver.Open(0) && receiver.LocalPort() != 0);
    CHECK(sender.Open(0) && sender.LocalPort() != 0);

    std::printf("nothing waiting: no datagram, no error\n");
    const auto started = std::chrono::steady_clock::now();
    CHECK(!receiver.Receive(buffer));
    CHECK(receiver.LastError() == 0);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(50)); // did not block

    std::printf("to 127.0.0.1\n");
    const std::uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
    CHECK(sender.SendTo(Lan::Loopback, receiver.LocalPort(), hello));
    auto received = waitFor(receiver, buffer);
    CHECK(received && received->Size == 5 && std::memcmp(buffer, hello, 5) == 0);
    CHECK(received && received->Address == Lan::Loopback && received->Port == sender.LocalPort());

    std::printf("to 255.255.255.255 on this machine\n");
    // A build machine may have no interface that can broadcast: then the send fails, and that is not this code's fault.
    const bool broadcastSent = sender.Broadcast(receiver.LocalPort(), hello);
    received = broadcastSent ? waitFor(receiver, buffer) : std::nullopt;
    if (received) {
        std::printf("  arrived from %s:%u (the interface broadcasts leave by, not 127.0.0.1)\n", text(received->Address).c_str(), received->Port);
    } else {
        std::printf("  did not arrive (sender error %d): a machine with no broadcast-capable interface up\n", sender.LastError());
    }
    if (received) CHECK(received->Size == 5 && received->Port == sender.LocalPort()); // a build machine may have no such interface

    std::printf("a datagram larger than the buffer is cut\n");
    std::uint8_t large[100];
    std::memset(large, 7, sizeof(large));
    CHECK(sender.SendTo(Lan::Loopback, receiver.LocalPort(), large));
    received = waitFor(receiver, std::span<std::uint8_t>(buffer, 10));
    CHECK(received && received->Size == 10);
    CHECK(!receiver.Receive(buffer)); // the rest of it is gone, not a second datagram

    std::printf("a port already taken\n");
    Lan::Endpoint second;
    CHECK(!second.Open(receiver.LocalPort()));
    CHECK(second.LastError() == EADDRINUSE);
    CHECK(!second.IsOpen());

    std::printf("closed\n");
    const auto port = receiver.LocalPort();
    receiver.Close();
    CHECK(!receiver.IsOpen() && !receiver.Receive(buffer) && !receiver.SendTo(Lan::Loopback, port, hello));
    CHECK(second.Open(port)); // free again

    if (failures) {
        std::printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("all endpoint tests passed\n");
    return 0;
}
