#include "Online/Lan.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// The computer's backend: BSD sockets, for host tests and bots. Same behaviour as LanPs5.cpp.
namespace Lan {

namespace {

sockaddr_in socketAddress(std::uint32_t address, std::uint16_t port) {
    sockaddr_in result{};
#ifdef __APPLE__
    result.sin_len = sizeof(result);
#endif
    result.sin_family = AF_INET;
    result.sin_port = htons(port);
    result.sin_addr.s_addr = htonl(address);
    return result;
}

}

Endpoint::~Endpoint() { Close(); }

bool Endpoint::Open(std::uint16_t port) {
    Close();
    const int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket < 0) {
        lastError = errno;
        return false;
    }
    const int enabled = 1;
    const int flags = ::fcntl(socket, F_GETFL, 0);
    auto local = socketAddress(0, port);
    socklen_t length = sizeof(local);
    if (::setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) != 0 || flags < 0 ||
        ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0 ||
        ::bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0 ||
        ::getsockname(socket, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
        lastError = errno;
        ::close(socket);
        return false;
    }
    handle = socket;
    localPort = ntohs(local.sin_port);
    lastError = 0;
    return true;
}

void Endpoint::Close() {
    if (handle < 0) return;
    ::close(handle);
    handle = -1;
    localPort = 0;
}

bool Endpoint::SendTo(std::uint32_t address, std::uint16_t port, std::span<const std::uint8_t> bytes) {
    if (handle < 0) return false;
    const auto target = socketAddress(address, port);
    const auto sent = ::sendto(handle, bytes.data(), bytes.size(), 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    if (sent < 0) {
        lastError = errno;
        return false;
    }
    return static_cast<std::size_t>(sent) == bytes.size();
}

std::optional<Received> Endpoint::Receive(std::span<std::uint8_t> buffer) {
    if (handle < 0) return std::nullopt;
    sockaddr_in from{};
    socklen_t length = sizeof(from);
    const auto size = ::recvfrom(handle, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&from), &length);
    if (size < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) lastError = errno;
        return std::nullopt;
    }
    if (from.sin_family != AF_INET) return std::nullopt;
    return Received{ntohl(from.sin_addr.s_addr), ntohs(from.sin_port), static_cast<std::size_t>(size)};
}

}
