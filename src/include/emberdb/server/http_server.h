#pragma once

#include "emberdb/common/config.h"
#include "emberdb/common/status.h"
#include "emberdb/emberdb.h"
#include <string>
#include <thread>
#include <atomic>
#include <functional>

namespace emberdb {

/**
 * Lightweight native HTTP 1.1 server exposing EmberDB query endpoints.
 */
class HttpServer {
public:
    explicit HttpServer(EmberDBInstance* db, int port = 8080);
    ~HttpServer();

    // Start server listening on port
    Status Start(bool background = true);

    // Stop server
    void Stop();

    bool IsRunning() const { return is_running_.load(); }
    int GetPort() const { return port_; }

    // Dispatch raw HTTP request and return full HTTP response string
    std::string HandleRequest(const std::string& method, const std::string& path, const std::string& body);

private:
    void RunServerLoop();

    EmberDBInstance* db_;
    int port_;
    std::atomic<bool> is_running_{false};
    std::thread worker_thread_;
    uintptr_t server_socket_{0}; // generic socket handle
};

} // namespace emberdb
