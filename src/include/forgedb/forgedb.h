#pragma once

#include "forgedb/common/config.h"
#include "forgedb/common/status.h"
#include <string>
#include <memory>

namespace forgedb {

/**
 * Main database instance interface.
 * Coordinates storage, catalog, execution, transactions, and recovery.
 */
class ForgeDBInstance {
public:
    explicit ForgeDBInstance(std::string db_directory)
        : db_directory_(std::move(db_directory)), is_open_(false) {}

    ~ForgeDBInstance() = default;

    Status Open() {
        is_open_ = true;
        return Status::OK();
    }

    Status Close() {
        is_open_ = false;
        return Status::OK();
    }

    bool IsOpen() const { return is_open_; }
    const std::string& GetDbDirectory() const { return db_directory_; }

private:
    std::string db_directory_;
    bool is_open_;
};

} // namespace forgedb
