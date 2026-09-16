#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include "emberdb/emberdb.h"
#include "emberdb/server/http_server.h"

namespace {
    std::atomic<bool> g_stop_requested{false};

    void SignalHandler(int) {
        g_stop_requested.store(true);
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string db_dir = "data";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "EmberDB HTTP REST Server\n" << std::endl;
            std::cout << "Usage: emberdb_server [options]\n" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -p, --port <port>    Port to listen on (default: 8080)" << std::endl;
            std::cout << "  -d, --db <dir>       Database storage directory (default: data)" << std::endl;
            std::cout << "  -h, --help           Show this help message" << std::endl;
            std::cout << "  -v, --version        Display engine version" << std::endl;
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "EmberDB v" << emberdb::EMBERDB_VERSION << std::endl;
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            }
        } else if (arg == "-d" || arg == "--db") {
            if (i + 1 < argc) {
                db_dir = argv[++i];
            }
        }
    }

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    emberdb::EmberDBInstance db(db_dir);
    auto st = db.Open();
    if (!st.ok()) {
        std::cerr << "Fatal: Failed to open database: " << st.ToString() << std::endl;
        return 1;
    }

    emberdb::HttpServer server(&db, port);
    st = server.Start(true);
    if (!st.ok()) {
        std::cerr << "Fatal: Failed to start HTTP server: " << st.ToString() << std::endl;
        db.Close();
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "EmberDB HTTP REST Server v" << emberdb::EMBERDB_VERSION << std::endl;
    std::cout << "Listening on: http://localhost:" << port << std::endl;
    std::cout << "Data directory: " << db_dir << std::endl;
    std::cout << "Press Ctrl+C to terminate." << std::endl;
    std::cout << "========================================" << std::endl;

    while (!g_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\nStopping HTTP server..." << std::endl;
    server.Stop();
    db.Close();
    std::cout << "EmberDB server terminated cleanly." << std::endl;
    return 0;
}
