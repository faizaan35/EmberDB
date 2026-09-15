#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "emberdb/common/config.h"
#include "emberdb/common/status.h"
#include "emberdb/storage/disk/disk_manager.h"
#include "emberdb/catalog/catalog.h"
#include "emberdb/sql/lexer/lexer.h"
#include "emberdb/sql/parser/parser.h"
#include "emberdb/execution/executor/execution_engine.h"

void PrintBanner() {
    std::cout << "========================================" << std::endl;
    std::cout << "EmberDB v" << emberdb::EMBERDB_VERSION 
              << " - From-Scratch Relational Database Engine" << std::endl;
    std::cout << "Type '.help' for commands or '.exit' to quit." << std::endl;
    std::cout << "========================================" << std::endl;
}

void PrintHelp() {
    std::cout << "EmberDB Command Line Interface" << std::endl;
    std::cout << "Usage: emberdb [options] [database_path]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  -h, --help       Show this help message" << std::endl;
    std::cout << "  -v, --version    Display version information" << std::endl;
    std::cout << "\nMeta Commands:" << std::endl;
    std::cout << "  .help            Show available meta commands" << std::endl;
    std::cout << "  .version         Show engine version" << std::endl;
    std::cout << "  .tables          List all database tables" << std::endl;
    std::cout << "  .schema [table]  Show table schema" << std::endl;
    std::cout << "  .exit, .quit     Exit the shell" << std::endl;
}

void RunRepl(const std::string& db_path) {
    emberdb::DiskManager disk_mgr(db_path);
    auto open_status = disk_mgr.Open();
    if (!open_status.ok()) {
        std::cerr << "Failed to open database file: " << open_status.ToString() << std::endl;
        return;
    }

    emberdb::Catalog catalog(&disk_mgr);
    auto cat_status = catalog.Init();
    if (!cat_status.ok()) {
        std::cerr << "Failed to initialize database catalog: " << cat_status.ToString() << std::endl;
        return;
    }

    emberdb::ExecutionEngine engine(&catalog);

    PrintBanner();
    std::cout << "Connected to database at: " << db_path << "\n" << std::endl;

    std::string line;
    std::string sql_buffer;

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

        if (sql_buffer.empty() && trimmed[0] == '.') {
            // Handle meta-commands
            if (trimmed == ".exit" || trimmed == ".quit") {
                std::cout << "Exiting EmberDB." << std::endl;
                break;
            } else if (trimmed == ".help") {
                PrintHelp();
            } else if (trimmed == ".version") {
                std::cout << "EmberDB version " << emberdb::EMBERDB_VERSION << std::endl;
            } else if (trimmed == ".tables") {
                auto tbl_names = catalog.GetAllTableNames();
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
                if (tname.empty()) {
                    for (const auto& name : catalog.GetAllTableNames()) {
                        auto* tbl = catalog.GetTable(name);
                        std::cout << "Table " << name << ": " << tbl->GetSchema().ToString() << std::endl;
                    }
                } else {
                    auto* tbl = catalog.GetTable(tname);
                    if (!tbl) {
                        std::cout << "Table not found: " << tname << std::endl;
                    } else {
                        std::cout << tbl->GetSchema().ToString() << std::endl;
                    }
                }
            } else {
                std::cout << "Unknown command: '" << trimmed << "'. Type .help for available commands." << std::endl;
            }
            continue;
        }

        // Accumulate SQL lines until semicolon or EOF
        if (!sql_buffer.empty()) {
            sql_buffer += " ";
        }
        sql_buffer += trimmed;

        if (sql_buffer.back() == ';') {
            emberdb::Lexer lexer(sql_buffer);
            auto tokens = lexer.Tokenize();
            emberdb::Parser parser(std::move(tokens));
            auto stmt_res = parser.Parse();

            if (!stmt_res.ok()) {
                std::cerr << stmt_res.status().ToString() << std::endl;
            } else {
                auto query_result = engine.Execute(stmt_res->get());
                std::cout << query_result.FormatAsTable();
            }
            sql_buffer.clear();
        }
    }

    disk_mgr.Close();
}

int main(int argc, char* argv[]) {
    std::string db_path = "data/default.db";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            PrintHelp();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "EmberDB v" << emberdb::EMBERDB_VERSION << std::endl;
            return 0;
        } else if (arg[0] != '-') {
            db_path = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            PrintHelp();
            return 1;
        }
    }

    RunRepl(db_path);
    return 0;
}
