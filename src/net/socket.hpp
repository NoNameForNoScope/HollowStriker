#ifndef BODYCAM_NET_SOCKET_HPP
#define BODYCAM_NET_SOCKET_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <map>

namespace bodycam {
namespace net {

struct Address {
    uint32_t ip = 0;     // ordinea gazdei (127.0.0.1 = 0x7F000001)
    uint16_t port = 0;
    bool operator==(const Address& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Address& o) const { return !(*this == o); }
    bool operator<(const Address& o) const { return ip != o.ip ? ip < o.ip : port < o.port; }
    std::string str() const;
    static bool parse(const std::string& s, Address& out);   // "ip:port" sau "nume:port"
};

struct Datagram {
    Address from;
    std::vector<uint8_t> data;
};

// Interfata minima de transport (datagrame neconfirmate).
class Socket {
public:
    virtual ~Socket() = default;
    virtual bool send_to(const Address& to, const uint8_t* data, size_t n) = 0;
    virtual bool receive(Datagram& out) = 0;     // non-blocant
    virtual Address local_address() const = 0;
};

// UDP real (Winsock / BSD sockets), non-blocant.
class UdpSocket : public Socket {
public:
    ~UdpSocket() override;
    bool open(uint16_t port);                    // 0 = port ales de sistem
    bool send_to(const Address& to, const uint8_t* data, size_t n) override;
    bool receive(Datagram& out) override;
    Address local_address() const override { return local_; }
private:
    intptr_t sock_ = -1;
    Address local_;
};

// Retea in memorie (teste, simulari fara placa de retea)
class LoopbackHub;
class LoopbackSocket : public Socket {
public:
    LoopbackSocket(LoopbackHub* hub, Address addr) : hub_(hub), addr_(addr) {}
    bool send_to(const Address& to, const uint8_t* data, size_t n) override;
    bool receive(Datagram& out) override;
    Address local_address() const override { return addr_; }
    std::deque<Datagram> inbox;
private:
    LoopbackHub* hub_;
    Address addr_;
};

class LoopbackHub {
public:
    std::shared_ptr<LoopbackSocket> create(uint16_t port);
    void deliver(const Address& from, const Address& to, const uint8_t* data, size_t n);
private:
    std::map<Address, std::weak_ptr<LoopbackSocket>> sockets_;
};

// Decorator: latenta, variatie (jitter) si pierdere de pachete simulate
class LaggedSocket : public Socket {
public:
    LaggedSocket(std::unique_ptr<Socket> inner, double latency_s, double jitter_s, double loss, uint32_t seed = 1)
        : inner_(std::move(inner)), latency_(latency_s), jitter_(jitter_s), loss_(loss), rng_(seed ? seed : 1) {}
    void set_time(double t) { now_ = t; }
    bool send_to(const Address& to, const uint8_t* data, size_t n) override;
    bool receive(Datagram& out) override;
    Address local_address() const override { return inner_->local_address(); }
private:
    struct Pending { double release; Address to; std::vector<uint8_t> data; };
    float frand();
    std::unique_ptr<Socket> inner_;
    double latency_, jitter_, loss_;
    double now_ = 0.0;
    uint32_t rng_;
    std::deque<Pending> out_;
};

}
}
#endif
