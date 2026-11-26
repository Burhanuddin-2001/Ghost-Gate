// Small blocking HTTP/1.1 server on Winsock: one short-lived thread per connection, a hard cap on concurrent connections, and strict timeouts.

#pragma once

#include "http.hpp"

#include <winsock2.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace gh::http {

class Server {
public:
    using Handler = std::function<Response(const Request&)>;

    struct Options {
        std::string bind_address   = "127.0.0.1";
        uint16_t    port           = 8080;   // 0 = pick a free port (use for testing)
        int         max_connections = 2;
        unsigned    io_timeout_ms   = 5000;  // per recv/send call
        unsigned    request_deadline_ms = 10000;   // whole request must arrive in this time
        Limits      limits;
    };

    Server(Handler handler, Options options);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Binds and starts accepting. On failure returns false and sets `error`.
    bool start(std::string& error);

    // Stops accepting and waits for in-flight connections to finish.
    void stop();

    uint16_t port() const { return port_; }

private:
    void accept_loop();
    void handle_connection(SOCKET client, const std::string& remote_ip);

    Handler     handler_;
    Options     options_;
    SOCKET      listen_ = INVALID_SOCKET;
    uint16_t    port_   = 0;
    bool        running_ = false;
    std::thread accept_thread_;

    std::mutex              mutex_;     // guards active_ and running_
    std::condition_variable idle_;
    int                     active_ = 0;
};

} // namespace gh::http
