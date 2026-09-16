#include <catch2/catch.hpp>
#include "emberdb/emberdb.h"
#include "emberdb/server/http_server.h"
#include <filesystem>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using sock_handle_t = SOCKET;
    constexpr sock_handle_t INVALID_SOCK = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using sock_handle_t = int;
    constexpr sock_handle_t INVALID_SOCK = -1;
#endif

using namespace emberdb;

namespace {
    const std::string TEST_API_DIR = "test_api_data";

    void CleanupApiDir() {
        std::error_code ec;
        std::filesystem::remove_all(TEST_API_DIR, ec);
    }
}

TEST_CASE("HTTP API: Health, Tables, Schema, and Query Endpoints", "[api]") {
    CleanupApiDir();

    EmberDBInstance db(TEST_API_DIR);
    REQUIRE(db.Open().ok());

    HttpServer server(&db, 18080);

    // 1. GET /api/health
    {
        std::string res = server.HandleRequest("GET", "/api/health", "");
        REQUIRE(res.find("HTTP/1.1 200 OK") != std::string::npos);
        REQUIRE(res.find("\"status\":\"ok\"") != std::string::npos);
        REQUIRE(res.find("\"engine\":\"EmberDB\"") != std::string::npos);
        REQUIRE(res.find("Access-Control-Allow-Origin: *") != std::string::npos);
    }

    // 2. CORS Preflight: OPTIONS /api/query
    {
        std::string res = server.HandleRequest("OPTIONS", "/api/query", "");
        REQUIRE(res.find("HTTP/1.1 204 No Content") != std::string::npos);
        REQUIRE(res.find("Access-Control-Allow-Methods:") != std::string::npos);
    }

    // 3. GET /api/tables when empty
    {
        std::string res = server.HandleRequest("GET", "/api/tables", "");
        REQUIRE(res.find("HTTP/1.1 200 OK") != std::string::npos);
        REQUIRE(res.find("\"tables\":[]") != std::string::npos);
    }

    // 4. POST /api/query - CREATE TABLE
    {
        std::string body = "{\"sql\":\"CREATE TABLE products (id INT, name VARCHAR, price DOUBLE);\"}";
        std::string res = server.HandleRequest("POST", "/api/query", body);
        REQUIRE(res.find("HTTP/1.1 200 OK") != std::string::npos);
        REQUIRE(res.find("\"success\":true") != std::string::npos);
    }

    // 5. GET /api/tables after table creation
    {
        std::string res = server.HandleRequest("GET", "/api/tables", "");
        REQUIRE(res.find("HTTP/1.1 200 OK") != std::string::npos);
        REQUIRE(res.find("\"products\"") != std::string::npos);
    }

    // 6. GET /api/schema/products
    {
        std::string res = server.HandleRequest("GET", "/api/schema/products", "");
        REQUIRE(res.find("HTTP/1.1 200 OK") != std::string::npos);
        REQUIRE(res.find("\"name\":\"id\"") != std::string::npos);
        REQUIRE(res.find("\"name\":\"name\"") != std::string::npos);
        REQUIRE(res.find("\"name\":\"price\"") != std::string::npos);
    }

    // 7. POST /api/query - INSERT
    {
        std::string body = "{\"sql\":\"INSERT INTO products VALUES (1, 'Laptop', 999.99);\"}";
        std::string res = server.HandleRequest("POST", "/api/query", body);
        REQUIRE(res.find("HTTP/1.1 200 OK") != std::string::npos);
        REQUIRE(res.find("\"success\":true") != std::string::npos);
        REQUIRE(res.find("\"rowsAffected\":1") != std::string::npos);
    }

    // 8. POST /api/query - SELECT
    {
        std::string body = "{\"sql\":\"SELECT * FROM products;\"}";
        std::string res = server.HandleRequest("POST", "/api/query", body);
        REQUIRE(res.find("HTTP/1.1 200 OK") != std::string::npos);
        REQUIRE(res.find("\"success\":true") != std::string::npos);
        REQUIRE(res.find("Laptop") != std::string::npos);
        REQUIRE(res.find("999.99") != std::string::npos);
        REQUIRE(res.find("\"columns\":[\"id\",\"name\",\"price\"]") != std::string::npos);
    }

    // 9. POST /api/query - Error case (invalid table)
    {
        std::string body = "{\"sql\":\"SELECT * FROM non_existent_table;\"}";
        std::string res = server.HandleRequest("POST", "/api/query", body);
        REQUIRE(res.find("HTTP/1.1 400 Bad Request") != std::string::npos);
        REQUIRE(res.find("\"success\":false") != std::string::npos);
        REQUIRE(res.find("\"error\":") != std::string::npos);
    }

    db.Close();
    CleanupApiDir();
}

TEST_CASE("HTTP API: Live TCP Socket Communication", "[api]") {
    CleanupApiDir();

    EmberDBInstance db(TEST_API_DIR);
    REQUIRE(db.Open().ok());

    int test_port = 19123;
    HttpServer server(&db, test_port);
    auto start_st = server.Start(true);
    REQUIRE(start_st.ok());
    REQUIRE(server.IsRunning());

    // Connect via client TCP socket
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    sock_handle_t client = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(client != INVALID_SOCK);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(static_cast<uint16_t>(test_port));

    int conn_res = connect(client, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    REQUIRE(conn_res == 0);

    // Send HTTP GET /api/health
    std::string http_req = "GET /api/health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    int sent = send(client, http_req.c_str(), static_cast<int>(http_req.size()), 0);
    REQUIRE(sent == static_cast<int>(http_req.size()));

    // Receive HTTP response
    std::vector<char> buffer(4096);
    int received = recv(client, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
    REQUIRE(received > 0);
    buffer[received] = '\0';
    std::string response(buffer.data());

    REQUIRE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    REQUIRE(response.find("\"status\":\"ok\"") != std::string::npos);

#ifdef _WIN32
    closesocket(client);
    WSACleanup();
#else
    close(client);
#endif

    server.Stop();
    REQUIRE_FALSE(server.IsRunning());

    db.Close();
    CleanupApiDir();
}
