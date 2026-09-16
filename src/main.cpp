#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <filesystem>
#include "emberdb/emberdb.h"
#include "emberdb/common/config.h"

void PrintBanner() {
    std::cout << "========================================" << std::endl;
    std::cout << "EmberDB v" << emberdb::EMBERDB_VERSION
              << " - Relational Database Engine" << std::endl;
    std::cout << "Type '.help' for commands or '.exit' to quit." << std::endl;
    std::cout << "========================================" << std::endl;
}

void PrintHelp() {
    std::cout << "EmberDB Command Line Interface (CLI)\n" << std::endl;
    std::cout << "Usage: emberdb [options] [database_directory]\n" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -c, --command <sql>  Execute SQL query non-interactively and exit" << std::endl;
    std::cout << "  -h, --help           Show this help message" << std::endl;
    std::cout << "  -v, --version        Display engine version\n" << std::endl;
    std::cout << "Meta Commands:" << std::endl;
    std::cout << "  .help                Show this help message" << std::endl;
    std::cout << "  .tables              List all tables in the database" << std::endl;
    std::cout << "  .schema [table]      Show column definitions and types" << std::endl;
    std::cout << "  .indexes [table]     List secondary indexes" << std::endl;
    std::cout << "  .stats               Display engine and buffer pool statistics" << std::endl;
    std::cout << "  .history             Display query history for this session" << std::endl;
    std::cout << "  .version             Show version information" << std::endl;
    std::cout << "  .exit, .quit         Cleanly flush and exit the shell" << std::endl;
}

void ShowSchema(emberdb::Catalog* catalog, const std::string& table_name) {
    if (!catalog) return;

    if (table_name.empty()) {
        auto names = catalog->GetAllTableNames();
        if (names.empty()) {
            std::cout << "No tables found." << std::endl;
            return;
        }
        for (const auto& name : names) {
            ShowSchema(catalog, name);
            std::cout << std::endl;
        }
        return;
    }

    auto* table = catalog->GetTable(table_name);
    if (!table) {
        std::cout << "Table '" << table_name << "' not found." << std::endl;
        return;
    }

    std::cout << "Table: " << table_name << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    const auto& schema = table->GetSchema();
    for (size_t i = 0; i < schema.GetColumnCount(); ++i) {
        const auto& col = schema.GetColumn(i);
        std::cout << std::left << std::setw(20) << col.GetName()
                  << std::setw(15) << emberdb::TypeIdToString(col.GetType()) << std::endl;
    }
}

void ShowIndexes(emberdb::Catalog* catalog, const std::string& table_name) {
    if (!catalog) return;

    std::vector<std::string> target_tables;
    if (table_name.empty()) {
        target_tables = catalog->GetAllTableNames();
    } else {
        target_tables.push_back(table_name);
    }

    bool found = false;
    for (const auto& tname : target_tables) {
        auto idxs = catalog->GetTableIndexes(tname);
        if (!idxs.empty()) {
            found = true;
            std::cout << "Table: " << tname << std::endl;
            std::cout << "----------------------------------------" << std::endl;
            for (auto* idx_info : idxs) {
                std::cout << "  " << std::left << std::setw(24) << idx_info->GetIndexName()
                          << " on (" << idx_info->GetColumnName() << ")" << std::endl;
            }
        }
    }

    if (!found) {
        std::cout << "No indexes found." << std::endl;
    }
}

void ShowStats(emberdb::EmberDBInstance& db) {
    auto* catalog = db.GetCatalog();
    auto* bpm = db.GetBufferPoolManager();
    auto* log_mgr = db.GetLogManager();

    std::cout << "EmberDB Engine Statistics:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Database Directory:   " << db.GetDbDirectory() << std::endl;
    if (catalog) {
        std::cout << "  Total Tables:         " << catalog->GetAllTableNames().size() << std::endl;
    }
    if (bpm) {
        std::cout << "  Buffer Pool Frames:   " << bpm->GetPoolSize() << std::endl;
    }
    if (log_mgr) {
        std::cout << "  Last Log LSN:         " << log_mgr->GetLastLSN() << std::endl;
        std::cout << "  Flushed Log LSN:      " << log_mgr->GetFlushedLSN() << std::endl;
    }
    std::cout << "========================================" << std::endl;
}

void RunRepl(emberdb::EmberDBInstance& db) {
    PrintBanner();
    std::cout << "Connected to database in: " << db.GetDbDirectory() << "\n" << std::endl;

    std::string line;
    std::string sql_buffer;
    std::vector<std::string> history;

    while (true) {
        if (sql_buffer.empty()) {
            std::cout << "EmberDB> ";
        } else {
            std::cout << "   ...> ";
        }

        if (!std::getline(std::cin, line)) {
            std::cout << "\nExiting EmberDB." << std::endl;
            break;
        }

        // Trim leading and trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;
        }
        size_t end = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(start, end - start + 1);

        // Strip single-line SQL comments (-- comment)
        if (trimmed.rfind("--", 0) == 0) {
            continue;
        }

        if (sql_buffer.empty() && trimmed[0] == '.') {
            if (trimmed == ".exit" || trimmed == ".quit") {
                std::cout << "Exiting EmberDB. Goodbye!" << std::endl;
                break;
            } else if (trimmed == ".help") {
                PrintHelp();
            } else if (trimmed == ".version") {
                std::cout << "EmberDB version " << emberdb::EMBERDB_VERSION << std::endl;
            } else if (trimmed == ".tables") {
                auto tbl_names = db.GetCatalog()->GetAllTableNames();
                if (tbl_names.empty()) {
                    std::cout << "No tables found." << std::endl;
                } else {
                    for (const auto& name : tbl_names) {
                        std::cout << "  " << name << std::endl;
                    }
                }
            } else if (trimmed.rfind(".schema", 0) == 0) {
                std::string tname;
                if (trimmed.size() > 7) {
                    size_t s = trimmed.find_first_not_of(" \t", 7);
                    if (s != std::string::npos) tname = trimmed.substr(s);
                }
                ShowSchema(db.GetCatalog(), tname);
            } else if (trimmed.rfind(".indexes", 0) == 0) {
                std::string tname;
                if (trimmed.size() > 8) {
                    size_t s = trimmed.find_first_not_of(" \t", 8);
                    if (s != std::string::npos) tname = trimmed.substr(s);
                }
                ShowIndexes(db.GetCatalog(), tname);
            } else if (trimmed == ".stats") {
                ShowStats(db);
            } else if (trimmed == ".history") {
                if (history.empty()) {
                    std::cout << "No queries in history." << std::endl;
                } else {
                    for (size_t i = 0; i < history.size(); ++i) {
                        std::cout << "  " << std::setw(3) << (i + 1) << "  " << history[i] << std::endl;
                    }
                }
            } else {
                std::cout << "Unknown command: '" << trimmed << "'. Type .help for available commands." << std::endl;
            }
            continue;
        }

        // Accumulate SQL lines until semicolon
        if (!sql_buffer.empty()) {
            sql_buffer += " ";
        }
        sql_buffer += trimmed;

        if (sql_buffer.back() == ';') {
            history.push_back(sql_buffer);
            auto query_result = db.ExecuteQuery(sql_buffer);
            std::cout << query_result.FormatAsTable();
            sql_buffer.clear();
        }
    }
}

int main(int argc, char* argv[]) {
    std::string db_dir = "data";
    std::string one_shot_command;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            PrintHelp();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "EmberDB v" << emberdb::EMBERDB_VERSION << std::endl;
            return 0;
        } else if (arg == "-c" || arg == "--command") {
            if (i + 1 < argc) {
                one_shot_command = argv[++i];
            } else {
                std::cerr << "Error: --command requires a SQL string argument." << std::endl;
                return 1;
            }
        } else if (arg[0] != '-') {
            db_dir = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            PrintHelp();
            return 1;
        }
    }

    emberdb::EmberDBInstance db(db_dir);
    auto status = db.Open();
    if (!status.ok()) {
        std::cerr << "Fatal: Failed to open EmberDB: " << status.ToString() << std::endl;
        return 1;
    }

    if (!one_shot_command.empty()) {
        std::vector<std::string> statements;
        std::string current;
        bool in_quotes = false;
        char quote_char = '\0';
        for (char c : one_shot_command) {
            if ((c == '\'' || c == '"') && (quote_char == '\0' || quote_char == c)) {
                in_quotes = !in_quotes;
                quote_char = in_quotes ? c : '\0';
            }
            current += c;
            if (c == ';' && !in_quotes) {
                statements.push_back(current);
                current.clear();
            }
        }
        if (!current.empty() && current.find_first_not_of(" \t\r\n") != std::string::npos) {
            statements.push_back(current);
        }

        bool all_ok = true;
        for (const auto& stmt : statements) {
            std::string trimmed = stmt;
            while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r' || trimmed.front() == '\n')) {
                trimmed.erase(0, 1);
            }
            if (trimmed.empty()) continue;
            auto res = db.ExecuteQuery(trimmed);
            std::cout << res.FormatAsTable();
            if (!res.success) {
                all_ok = false;
                break;
            }
        }
        db.Close();
        return all_ok ? 0 : 1;
    }

    RunRepl(db);
    db.Close();
    return 0;
}
