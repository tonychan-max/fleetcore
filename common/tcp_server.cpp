#include "common/tcp_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace fleetcore {

namespace {

// Puts a descriptor into non-blocking mode so the main loop is never stuck
// waiting on the socket -- it still has a message queue to service.
bool set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

TcpServer::~TcpServer() {
    if (client_fd_ >= 0) ::close(client_fd_);
    if (listen_fd_ >= 0) ::close(listen_fd_);
}

bool TcpServer::listen(uint16_t port) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::fprintf(stderr, "socket() failed: %s\n", std::strerror(errno));
        return false;
    }

    // Without SO_REUSEADDR the port stays unusable for a couple of minutes
    // after the process exits, because the kernel keeps the old connection
    // in TIME_WAIT. That makes restarting during development painful.
    const int on = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        std::fprintf(stderr, "bind(%u) failed: %s\n", port, std::strerror(errno));
        return false;
    }

    // Backlog 1: one terminal per termd.
    if (::listen(listen_fd_, 1) < 0) {
        std::fprintf(stderr, "listen() failed: %s\n", std::strerror(errno));
        return false;
    }

    if (!set_nonblocking(listen_fd_)) {
        std::fprintf(stderr, "fcntl() failed: %s\n", std::strerror(errno));
        return false;
    }
    return true;
}

bool TcpServer::poll_accept() {
    if (listen_fd_ < 0) return false;

    const int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) return false;   // EAGAIN when nothing is pending

    // A reconnecting terminal replaces the old socket. The old one is stale
    // by definition -- the terminal would not be reconnecting otherwise.
    if (client_fd_ >= 0) ::close(client_fd_);

    client_fd_ = fd;
    set_nonblocking(client_fd_);

    // Commands are small and latency matters more than efficiency here, so
    // do not let the kernel hold them back waiting for more data.
    const int on = 1;
    ::setsockopt(client_fd_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

    return true;
}

long TcpServer::poll_read(uint8_t* buf, std::size_t cap) {
    if (client_fd_ < 0) return 0;

    const ssize_t n = ::recv(client_fd_, buf, cap, 0);
    if (n > 0) return n;

    if (n == 0) {
        // Orderly shutdown by the peer.
        drop_client();
        return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    if (errno == EINTR) return 0;

    drop_client();
    return -1;
}

bool TcpServer::send(const uint8_t* data, std::size_t len) {
    if (client_fd_ < 0) return false;

    // send() may accept fewer bytes than asked for. Looping is required;
    // assuming one send() writes everything is the same class of bug as
    // assuming one recv() returns exactly one message.
    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(client_fd_, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EINTR)) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Socket buffer full. Retrying here would spin; for Step 6 the
            // messages are small enough that this should not happen.
            // Phase 4's store-and-forward is the real answer.
            continue;
        }
        drop_client();
        return false;
    }
    return true;
}

void TcpServer::drop_client() {
    if (client_fd_ >= 0) {
        ::close(client_fd_);
        client_fd_ = -1;
    }
}

}  // namespace fleetcore