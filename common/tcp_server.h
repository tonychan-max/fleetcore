#pragma once

#include <cstddef>
#include <cstdint>

namespace fleetcore {

// ============================================================
// Minimal single-client TCP server.
//
// One termd serves one terminal, so there is no need for a connection table
// or an accept loop that keeps several clients (ARCHITECTURE section 3-3:
// one-to-one needs no funnel). If the terminal reconnects, the previous
// socket is dropped and the new one takes over.
//
// Qt is deliberately not used here: termd is a plain daemon. The terminal
// side uses QTcpSocket. Both ends speak the same bytes, which is the point --
// the contract does not depend on the implementation.
// ============================================================

class TcpServer {
public:
    TcpServer() = default;
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool listen(uint16_t port);

    // Accepts a pending connection if there is one. Non-blocking.
    // Returns true when a new client was accepted.
    bool poll_accept();

    // Reads whatever is available. Non-blocking.
    // Returns bytes read, 0 if nothing waiting, -1 if the client went away.
    long poll_read(uint8_t* buf, std::size_t cap);

    bool send(const uint8_t* data, std::size_t len);

    bool has_client() const { return client_fd_ >= 0; }
    void drop_client();

private:
    int listen_fd_ = -1;
    int client_fd_ = -1;
};

}  // namespace fleetcore