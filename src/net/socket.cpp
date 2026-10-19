#include "socket.hpp"
#include <cstring>
#include <cstdio>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace bodycam {
namespace net {

namespace {
#ifdef _WIN32
struct WinsockInit {
    bool ok = false;
    WinsockInit() { WSADATA d; ok = WSAStartup(MAKEWORD(2, 2), &d) == 0; }
    ~WinsockInit() { if (ok) WSACleanup(); }
};
bool ensure_winsock() { static WinsockInit init; return init.ok; }
#else
bool ensure_winsock() { return true; }
#endif
}

std::string Address::str() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, port);
    return buf;
}

bool Address::parse(const std::string& s, Address& out) {
    size_t colon = s.rfind(':');
    if (colon == std::string::npos) return false;
    std::string host = s.substr(0, colon);
    int port = std::atoi(s.substr(colon + 1).c_str());
    if (port <= 0 || port > 65535) return false;
    if (!ensure_winsock()) return false;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
    out.ip = ntohl(sin->sin_addr.s_addr);
    out.port = static_cast<uint16_t>(port);
    freeaddrinfo(res);
    return true;
}

// ---------------------------------------------------------------------------
UdpSocket::~UdpSocket() {
    if (sock_ != -1) {
#ifdef _WIN32
        closesocket(static_cast<SOCKET>(sock_));
#else
        ::close(static_cast<int>(sock_));
#endif
    }
}

bool UdpSocket::open(uint16_t port) {
    if (!ensure_winsock()) return false;
    auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return false;
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    if (s < 0) return false;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    sock_ = static_cast<intptr_t>(s);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    local_.ip = 0x7F000001;
    local_.port = ntohs(bound.sin_port);
    return true;
}

bool UdpSocket::send_to(const Address& to, const uint8_t* data, size_t n) {
    if (sock_ == -1) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port = htons(to.port);
    auto r = ::sendto(static_cast<decltype(::socket(0, 0, 0))>(sock_), reinterpret_cast<const char*>(data), static_cast<int>(n), 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return r == static_cast<int>(n);
}

bool UdpSocket::receive(Datagram& out) {
    if (sock_ == -1) return false;
    uint8_t buf[65536];
    sockaddr_in from{};
    socklen_t len = sizeof(from);
    auto r = ::recvfrom(static_cast<decltype(::socket(0, 0, 0))>(sock_), reinterpret_cast<char*>(buf), sizeof(buf), 0,
                        reinterpret_cast<sockaddr*>(&from), &len);
    if (r <= 0) return false;
    out.from.ip = ntohl(from.sin_addr.s_addr);
    out.from.port = ntohs(from.sin_port);
    out.data.assign(buf, buf + r);
    return true;
}

// ---------------------------------------------------------------------------
bool LoopbackSocket::send_to(const Address& to, const uint8_t* data, size_t n) {
    hub_->deliver(addr_, to, data, n);
    return true;
}

bool LoopbackSocket::receive(Datagram& out) {
    if (inbox.empty()) return false;
    out = std::move(inbox.front());
    inbox.pop_front();
    return true;
}

std::shared_ptr<LoopbackSocket> LoopbackHub::create(uint16_t port) {
    Address a{0x7F000001, port};
    auto s = std::make_shared<LoopbackSocket>(this, a);
    sockets_[a] = s;
    return s;
}

void LoopbackHub::deliver(const Address& from, const Address& to, const uint8_t* data, size_t n) {
    auto it = sockets_.find(to);
    if (it == sockets_.end()) return;
    auto s = it->second.lock();
    if (!s) return;
    Datagram d;
    d.from = from;
    d.data.assign(data, data + n);
    s->inbox.push_back(std::move(d));
}

// ---------------------------------------------------------------------------
float LaggedSocket::frand() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFFFF) / 16777215.0f;
}

bool LaggedSocket::send_to(const Address& to, const uint8_t* data, size_t n) {
    if (frand() < loss_) return true;      // pierdut pe drum
    double delay = latency_ + (frand() - 0.5) * 2.0 * jitter_;
    out_.push_back({now_ + std::max(0.0, delay), to, std::vector<uint8_t>(data, data + n)});
    return true;
}

bool LaggedSocket::receive(Datagram& out) {
    // pachetele intarziate pleaca abia cand le vine timpul
    for (auto it = out_.begin(); it != out_.end();) {
        if (it->release <= now_) {
            inner_->send_to(it->to, it->data.data(), it->data.size());
            it = out_.erase(it);
        } else ++it;
    }
    return inner_->receive(out);
}

}
}
