#include "http_server.hpp"

#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <system_error>

namespace gh::http {
namespace {

// WSAStartup once per process, WSACleanup at exit.
struct WinsockRuntime {
    bool ok = false;
    WinsockRuntime() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockRuntime() { if (ok) WSACleanup(); }
};

bool winsock_ready() {
    static WinsockRuntime runtime;
    return runtime.ok;
}

// Owns one SOCKET; closes it on scope exit.
class UniqueSocket {
public:
    explicit UniqueSocket(SOCKET s) : s_(s) {}
    ~UniqueSocket() { if (s_ != INVALID_SOCKET) closesocket(s_); }
    UniqueSocket(const UniqueSocket&) = delete;
    UniqueSocket& operator=(const UniqueSocket&) = delete;
    SOCKET get() const { return s_; }
private:
    SOCKET s_;
};

bool send_all(SOCKET s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const size_t chunk = std::min<size_t>(data.size() - sent, 64 * 1024);
        const int n = send(s, data.data() + sent, static_cast<int>(chunk), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

void set_timeouts(SOCKET s, unsigned ms) {
    const DWORD t = ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
}

// Half-close, then drain briefly so the peer receives the full response
// instead of a reset when it still had unread bytes in flight.
void graceful_close(SOCKET s) {
    shutdown(s, SD_SEND);
    set_timeouts(s, 500);
    char sink[1024];
    for (int i = 0; i < 16 && recv(s, sink, sizeof(sink), 0) > 0; ++i) {}
}

} // namespace

Server::Server(Handler handler, Options options)
    : handler_(std::move(handler)), options_(std::move(options)) {}

Server::~Server() { stop(); }

bool Server::start(std::string& error) {
    if (!winsock_ready()) {
        error = "Winsock initialisation failed";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(options_.port);
    if (inet_pton(AF_INET, options_.bind_address.c_str(), &addr.sin_addr) != 1) {
        error = "invalid bind address: " + options_.bind_address;
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = "socket() failed: " + std::to_string(WSAGetLastError());
        return false;
    }
    // Stop other processes from binding the same port and stealing connections.
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
               sizeof(exclusive));

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        const int code = WSAGetLastError();
        closesocket(s);
        error = code == WSAEADDRINUSE || code == WSAEACCES
                    ? "port " + std::to_string(options_.port) + " is already in use"
                    : "bind() failed: " + std::to_string(code);
        return false;
    }
    if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
        error = "listen() failed: " + std::to_string(WSAGetLastError());
        closesocket(s);
        return false;
    }

    sockaddr_in bound{};
    int len = sizeof(bound);
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    port_ = ntohs(bound.sin_port);

    listen_ = s;
    {
        std::lock_guard lock(mutex_);
        running_ = true;
    }
    accept_thread_ = std::thread([this] { accept_loop(); });
    return true;
}

void Server::stop() {
    {
        std::lock_guard lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    // Closing the listening socket makes the blocking accept() return.
    closesocket(listen_);
    listen_ = INVALID_SOCKET;
    if (accept_thread_.joinable()) accept_thread_.join();

    // Connection threads are detached; wait until every one has finished.
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return active_ == 0; });
}

void Server::accept_loop() {
    const SOCKET listener = listen_;
    for (;;) {
        sockaddr_in addr{};
        int len = sizeof(addr);
        SOCKET client = accept(listener, reinterpret_cast<sockaddr*>(&addr), &len);

        bool over_capacity = false;
        {
            std::lock_guard lock(mutex_);
            if (!running_) {
                if (client != INVALID_SOCKET) closesocket(client);
                return;
            }
            if (client != INVALID_SOCKET) {
                if (active_ >= options_.max_connections) {
                    over_capacity = true;
                } else {
                    ++active_;
                }
            }
        }
        if (client == INVALID_SOCKET) {
            Sleep(10);   // transient error (e.g. peer reset before accept); avoid spinning
            continue;
        }

        if (over_capacity) {
            UniqueSocket guard(client);
            set_timeouts(client, 1000);
            send_all(client, serialize(text(503, "server busy\n")));
            continue;
        }

        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        std::string remote_ip = ip;

        try {
            std::thread([this, client, remote_ip] {
                handle_connection(client, remote_ip);
                // Notify while holding the lock: once we release it, stop() may
                // return and destroy this Server, so touch nothing afterwards.
                std::lock_guard lock(mutex_);
                --active_;
                idle_.notify_all();
            }).detach();
        } catch (const std::system_error&) {
            closesocket(client);   // could not start a thread: drop this connection
            std::lock_guard lock(mutex_);
            --active_;
            idle_.notify_all();
        }
    }
}

void Server::handle_connection(SOCKET client, const std::string& remote_ip) {
    UniqueSocket guard(client);
    set_timeouts(client, options_.io_timeout_ms);

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(options_.request_deadline_ms);
    std::string buffer;
    Request request;
    ParseResult result = ParseResult::Incomplete;
    char chunk[4096];

    while (result == ParseResult::Incomplete) {
        if (std::chrono::steady_clock::now() > deadline) {
            send_all(client, serialize(text(408, "request timeout\n")));
            return;
        }
        const int n = recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return;   // peer closed, timed out, or errored: nothing useful to answer
        }
        buffer.append(chunk, static_cast<size_t>(n));
        result = parse_request(buffer, request, options_.limits);
    }

    Response response;
    if (result == ParseResult::Bad) {
        response = text(400, "bad request\n");
    } else if (result == ParseResult::TooLarge) {
        response = text(413, "request too large\n");
    } else {
        request.remote_ip = remote_ip;
        try {
            response = handler_(request);
        } catch (...) {
            response = text(500, "internal error\n");
        }
    }

    send_all(client, serialize(response, result == ParseResult::Ok && request.method == "HEAD"));
    graceful_close(client);
}

} // namespace gh::http
