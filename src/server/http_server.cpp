#include "emberdb/server/http_server.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

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

size_t ParseContentLength(const std::string& headers) {
    std::string lower_headers = headers;
    for (char& c : lower_headers) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    size_t pos = lower_headers.find("content-length:");
    if (pos == std::string::npos) {
        return 0;
    }
    pos += 15; // length of "content-length:"
    while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) {
        ++pos;
    }
    size_t end_pos = headers.find_first_of("\r\n", pos);
    std::string len_str = (end_pos == std::string::npos) ? headers.substr(pos) : headers.substr(pos, end_pos - pos);
    try {
        long long len = std::stoll(len_str);
        return (len > 0) ? static_cast<size_t>(len) : 0;
    } catch (...) {
        return 0;
    }
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

        // Apply a receive timeout on client socket to prevent hanging on stalled connections
#ifdef _WIN32
        DWORD client_timeout = 3000; // 3 seconds timeout
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&client_timeout), sizeof(client_timeout));
#else
        struct timeval client_tv;
        client_tv.tv_sec = 3;
        client_tv.tv_usec = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &client_tv, sizeof(client_tv));
#endif

        std::string raw_data;
        std::vector<char> chunk(4096);
        size_t header_end = std::string::npos;
        size_t header_delim_len = 0;

        // 1. Read until complete HTTP header terminator is received (\r\n\r\n or \n\n)
        while (raw_data.size() < 65536) { // 64KB max header size safeguard
            header_end = raw_data.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                header_delim_len = 4;
                break;
            }
            header_end = raw_data.find("\n\n");
            if (header_end != std::string::npos) {
                header_delim_len = 2;
                break;
            }

            int n = recv(client, chunk.data(), static_cast<int>(chunk.size()), 0);
            if (n <= 0) {
                break; // Socket closed, timed out, or error
            }
            raw_data.append(chunk.data(), static_cast<size_t>(n));
        }

        if (header_end != std::string::npos) {
            std::string headers_str = raw_data.substr(0, header_end);
            std::string body = raw_data.substr(header_end + header_delim_len);

            // Parse request line (method, path)
            std::string method, path;
            size_t first_space = headers_str.find(' ');
            if (first_space != std::string::npos) {
                method = headers_str.substr(0, first_space);
                size_t second_space = headers_str.find(' ', first_space + 1);
                if (second_space != std::string::npos) {
                    path = headers_str.substr(first_space + 1, second_space - first_space - 1);
                }
            }

            // 2. Parse Content-Length header
            size_t content_length = ParseContentLength(headers_str);

            // 3. If Content-Length > 0, read remaining body bytes from socket
            bool read_ok = true;
            while (body.size() < content_length) {
                size_t remaining = content_length - body.size();
                size_t to_read = std::min(chunk.size(), remaining);
                int n = recv(client, chunk.data(), static_cast<int>(to_read), 0);
                if (n <= 0) {
                    read_ok = false;
                    break;
                }
                body.append(chunk.data(), static_cast<size_t>(n));
            }

            // Body larger than declared Content-Length: truncate to declared length
            if (content_length > 0 && body.size() > content_length) {
                body.resize(content_length);
            }

            // 4. Dispatch request
            if (!read_ok) {
                std::string err_resp = BuildHttpResponse(400, "Bad Request", "application/json",
                    "{\"success\":false,\"error\":\"Client disconnected before complete request body was received\"}");
                send(client, err_resp.data(), static_cast<int>(err_resp.size()), 0);
            } else {
                std::string response = HandleRequest(method, path, body);
                send(client, response.data(), static_cast<int>(response.size()), 0);
            }
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
