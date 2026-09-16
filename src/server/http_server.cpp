#include "emberdb/server/http_server.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCKET_FD = INVALID_SOCKET;
    constexpr int SOCKET_ERROR_VAL = SOCKET_ERROR;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using socket_t = int;
    constexpr socket_t INVALID_SOCKET_FD = -1;
    constexpr int SOCKET_ERROR_VAL = -1;
#endif

namespace emberdb {

namespace {

void CloseSocketFd(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

std::string EscapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string ExtractSqlFromJson(const std::string& body) {
    auto pos = body.find("\"sql\"");
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = body.find('"', pos);
    if (pos == std::string::npos) return "";
    size_t start = pos + 1;
    std::string result;
    bool escaped = false;
    for (size_t i = start; i < body.size(); ++i) {
        char c = body[i];
        if (escaped) {
            if (c == '"') result += '"';
            else if (c == '\\') result += '\\';
            else if (c == 'n') result += '\n';
            else if (c == 't') result += '\t';
            else if (c == 'r') result += '\r';
            else result += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

std::string QueryResultToJson(const QueryResult& res) {
    std::ostringstream oss;
    if (!res.success) {
        oss << "{\"success\":false,\"error\":\"" << EscapeJsonString(res.error_message) << "\"}";
        return oss.str();
    }

    oss << "{\"success\":true,\"columns\":[";
    const auto& cols = res.schema.GetColumns();
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << EscapeJsonString(cols[i].GetName()) << "\"";
    }
    oss << "],\"rows\":[";
    for (size_t r = 0; r < res.rows.size(); ++r) {
        if (r > 0) oss << ",";
        oss << "[";
        for (size_t c = 0; c < cols.size(); ++c) {
            if (c > 0) oss << ",";
            Value v = res.rows[r].GetValue(res.schema, static_cast<uint32_t>(c));
            if (v.IsNull()) {
                oss << "null";
            } else {
                switch (v.GetTypeId()) {
                    case TypeId::BOOLEAN:
                        oss << (v.GetAsBoolean() ? "true" : "false");
                        break;
                    case TypeId::INTEGER:
                        oss << v.GetAsInteger();
                        break;
                    case TypeId::BIGINT:
                        oss << v.GetAsBigInt();
                        break;
                    case TypeId::DOUBLE:
                        oss << v.GetAsDouble();
                        break;
                    case TypeId::VARCHAR:
                        oss << "\"" << EscapeJsonString(v.GetAsVarChar()) << "\"";
                        break;
                    default:
                        oss << "null";
                        break;
                }
            }
        }
        oss << "]";
    }
    oss << "],\"rowsAffected\":" << res.rows_affected;
    oss << ",\"executionTimeMs\":" << res.execution_time_ms << "}";
    return oss.str();
}

std::string BuildHttpResponse(int status_code, const std::string& status_text,
                              const std::string& content_type, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "Access-Control-Allow-Origin: *\r\n";
    oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    oss << "Access-Control-Allow-Headers: Content-Type\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

} // namespace

HttpServer::HttpServer(EmberDBInstance* db, int port)
    : db_(db), port_(port) {}

HttpServer::~HttpServer() {
    Stop();
}

Status HttpServer::Start(bool background) {
    if (is_running_.load()) {
        return Status::OK();
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return Status::IOError("Failed to initialize Windows Sockets");
    }
#endif

    socket_t listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET_FD) {
        return Status::IOError("Failed to create socket");
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    DWORD timeout = 500; // 500 ms timeout for accept polling
    setsockopt(listen_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(listen_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VAL) {
        CloseSocketFd(listen_sock);
        return Status::IOError("Failed to bind socket to port " + std::to_string(port_));
    }

    if (listen(listen_sock, 16) == SOCKET_ERROR_VAL) {
        CloseSocketFd(listen_sock);
        return Status::IOError("Failed to listen on socket");
    }

    server_socket_ = static_cast<uintptr_t>(listen_sock);
    is_running_.store(true);

    if (background) {
        worker_thread_ = std::thread(&HttpServer::RunServerLoop, this);
    } else {
        RunServerLoop();
    }

    return Status::OK();
}

void HttpServer::Stop() {
    if (!is_running_.load()) {
        return;
    }

    is_running_.store(false);

    if (server_socket_ != 0) {
        CloseSocketFd(static_cast<socket_t>(server_socket_));
        server_socket_ = 0;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

void HttpServer::RunServerLoop() {
    socket_t listen_sock = static_cast<socket_t>(server_socket_);

    while (is_running_.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms

        int sel = select(static_cast<int>(listen_sock + 1), &read_fds, nullptr, nullptr, &tv);
        if (sel <= 0) {
            continue;
        }

        sockaddr_in client_addr{};
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        socket_t client = accept(listen_sock, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client == INVALID_SOCKET_FD) {
            continue;
        }

        std::vector<char> buffer(16384);
        int bytes_read = recv(client, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            std::string req_str(buffer.data(), bytes_read);

            // Parse request line
            std::string method, path;
            size_t first_space = req_str.find(' ');
            if (first_space != std::string::npos) {
                method = req_str.substr(0, first_space);
                size_t second_space = req_str.find(' ', first_space + 1);
                if (second_space != std::string::npos) {
                    path = req_str.substr(first_space + 1, second_space - first_space - 1);
                }
            }

            // Parse body
            std::string body;
            size_t header_end = req_str.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                body = req_str.substr(header_end + 4);
            }

            std::string response = HandleRequest(method, path, body);
            send(client, response.data(), static_cast<int>(response.size()), 0);
        }

#ifdef _WIN32
        shutdown(client, SD_SEND);
#else
        shutdown(client, SHUT_WR);
#endif
        CloseSocketFd(client);
    }
}

std::string HttpServer::HandleRequest(const std::string& method, const std::string& path, const std::string& body) {
    // CORS Preflight
    if (method == "OPTIONS") {
        return BuildHttpResponse(204, "No Content", "text/plain", "");
    }

    if (!db_ || !db_->IsOpen()) {
        return BuildHttpResponse(503, "Service Unavailable", "application/json",
            "{\"success\":false,\"error\":\"Database instance is not ready\"}");
    }

    // Health check
    if (method == "GET" && path == "/api/health") {
        std::string json = "{\"status\":\"ok\",\"version\":\"" + std::string(EMBERDB_VERSION) + "\",\"engine\":\"EmberDB\"}";
        return BuildHttpResponse(200, "OK", "application/json", json);
    }

    // Tables list
    if (method == "GET" && path == "/api/tables") {
        auto tables = db_->GetCatalog()->GetAllTableNames();
        std::ostringstream oss;
        oss << "{\"success\":true,\"tables\":[";
        for (size_t i = 0; i < tables.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << EscapeJsonString(tables[i]) << "\"";
        }
        oss << "]}";
        return BuildHttpResponse(200, "OK", "application/json", oss.str());
    }

    // Schema inspection
    if (method == "GET" && (path.rfind("/api/schema/", 0) == 0 || path.rfind("/api/schema?table=", 0) == 0)) {
        std::string table_name;
        if (path.rfind("/api/schema/", 0) == 0) {
            table_name = path.substr(12);
        } else {
            table_name = path.substr(18);
        }

        Table* table = db_->GetCatalog()->GetTable(table_name);
        if (!table) {
            return BuildHttpResponse(404, "Not Found", "application/json",
                "{\"success\":false,\"error\":\"Table '" + EscapeJsonString(table_name) + "' does not exist\"}");
        }

        const auto& schema = table->GetSchema();
        std::ostringstream oss;
        oss << "{\"success\":true,\"table\":\"" << EscapeJsonString(table_name) << "\",\"columns\":[";
        for (size_t i = 0; i < schema.GetColumnCount(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"name\":\"" << EscapeJsonString(schema.GetColumn(i).GetName())
                << "\",\"type\":\"" << TypeIdToString(schema.GetColumn(i).GetType()) << "\"}";
        }
        oss << "]}";
        return BuildHttpResponse(200, "OK", "application/json", oss.str());
    }

    // SQL Query execution
    if (method == "POST" && path == "/api/query") {
        std::string sql = ExtractSqlFromJson(body);
        if (sql.empty()) {
            return BuildHttpResponse(400, "Bad Request", "application/json",
                "{\"success\":false,\"error\":\"Invalid request: 'sql' field missing in JSON body\"}");
        }

        QueryResult res = db_->ExecuteQuery(sql);
        std::string json = QueryResultToJson(res);
        int code = res.success ? 200 : 400;
        return BuildHttpResponse(code, res.success ? "OK" : "Bad Request", "application/json", json);
    }

    // Static Web UI assets (web/dist)
    if (method == "GET") {
        std::string rel_path = path;
        if (rel_path == "/" || rel_path.empty()) {
            rel_path = "/index.html";
        }
        std::vector<std::string> base_candidates = {"web/dist", "../web/dist", "../../web/dist"};
        for (const auto& base : base_candidates) {
            std::error_code ec;
            std::filesystem::path target = std::filesystem::path(base + rel_path);
            if (std::filesystem::exists(target, ec) && !std::filesystem::is_directory(target, ec)) {
                std::ifstream f(target, std::ios::binary);
                if (f) {
                    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    std::string content_type = "text/plain";
                    auto ext = target.extension().string();
                    if (ext == ".html") content_type = "text/html; charset=utf-8";
                    else if (ext == ".js") content_type = "application/javascript; charset=utf-8";
                    else if (ext == ".css") content_type = "text/css; charset=utf-8";
                    else if (ext == ".svg") content_type = "image/svg+xml";
                    else if (ext == ".json") content_type = "application/json";
                    return BuildHttpResponse(200, "OK", content_type, content);
                }
            }
        }
    }

    return BuildHttpResponse(404, "Not Found", "application/json",
        "{\"success\":false,\"error\":\"Endpoint not found: " + EscapeJsonString(path) + "\"}");
}

} // namespace emberdb
