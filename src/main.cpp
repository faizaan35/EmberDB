#include <iostream>
#include <string>
#include <vector>
#include "forgedb/common/config.h"
#include "forgedb/common/status.h"
#include "forgedb/forgedb.h"

void PrintBanner() {
    std::cout << "========================================" << std::endl;
    std::cout << "ForgeDB v" << forgedb::FORGEDB_VERSION 
              << " - From-Scratch Relational Database Engine" << std::endl;
    std::cout << "Type '.help' for commands or '.exit' to quit." << std::endl;
    std::cout << "========================================" << std::endl;
}

void PrintHelp() {
    std::cout << "ForgeDB Command Line Interface" << std::endl;
    std::cout << "Usage: forgedb [options] [database_path]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  -h, --help       Show this help message" << std::endl;
    std::cout << "  -v, --version    Display version information" << std::endl;
    std::cout << "\nMeta Commands:" << std::endl;
    std::cout << "  .help            Show available meta commands" << std::endl;
    std::cout << "  .version         Show engine version" << std::endl;
    std::cout << "  .exit, .quit     Exit the shell" << std::endl;
}

void RunRepl(const std::string& db_path) {
    forgedb::ForgeDBInstance db(db_path);
    auto status = db.Open();
    if (!status.ok()) {
        std::cerr << "Failed to open database: " << status.ToString() << std::endl;
        return;
    }

    PrintBanner();
    std::cout << "Connected to database at: " << db_path << "\n" << std::endl;

    std::string line;
    while (true) {
        std::cout << "ForgeDB> ";
        if (!std::getline(std::cin, line)) {
            std::cout << "\nExiting ForgeDB." << std::endl;
            break;
        }

        // Trim leading and trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;
        }
        size_t end = line.find_last_not_of(" \t\r\n");
        std::string command = line.substr(start, end - start + 1);

        if (command == ".exit" || command == ".quit") {
            std::cout << "Exiting ForgeDB." << std::endl;
            break;
        } else if (command == ".help") {
            PrintHelp();
        } else if (command == ".version") {
            std::cout << "ForgeDB version " << forgedb::FORGEDB_VERSION << std::endl;
        } else {
            std::cout << "Unrecognized command: '" << command 
                      << "'. (SQL execution will be available in Phase 4. Type .help for info)" 
                      << std::endl;
        }
    }

    db.Close();
}

int main(int argc, char* argv[]) {
    std::string db_path = "data/default.db";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            PrintHelp();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "ForgeDB v" << forgedb::FORGEDB_VERSION << std::endl;
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
