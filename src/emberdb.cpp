#include "emberdb/emberdb.h"
#include "emberdb/sql/lexer/lexer.h"
#include "emberdb/sql/parser/parser.h"
#include <filesystem>

namespace emberdb {

EmberDBInstance::EmberDBInstance(std::string db_directory, size_t buffer_pool_size)
    : db_directory_(std::move(db_directory)), buffer_pool_size_(buffer_pool_size) {}

EmberDBInstance::~EmberDBInstance() {
    if (is_open_) {
        Close();
    }
}

Status EmberDBInstance::Open() {
    if (is_open_) {
        return Status::OK();
    }

    std::filesystem::create_directories(db_directory_);
    std::string db_path = (std::filesystem::path(db_directory_) / "ember.db").string();
    std::string wal_path = (std::filesystem::path(db_directory_) / "ember.wal").string();

    disk_mgr_ = std::make_unique<DiskManager>(db_path);
    auto st = disk_mgr_->Open();
    if (!st.ok()) return st;

    bpm_ = std::make_unique<BufferPoolManager>(buffer_pool_size_, disk_mgr_.get());

    log_mgr_ = std::make_unique<LogManager>(wal_path);
    st = log_mgr_->Open();
    if (!st.ok()) return st;

    bpm_->SetLogManager(log_mgr_.get());

    catalog_ = std::make_unique<Catalog>(bpm_.get());
    st = catalog_->Init();
    if (!st.ok()) return st;

    recovery_mgr_ = std::make_unique<RecoveryManager>(disk_mgr_.get(), bpm_.get(), catalog_.get(), log_mgr_.get());
    if (recovery_mgr_->NeedsRecovery()) {
        auto rec_res = recovery_mgr_->Recover();
        if (!rec_res.ok()) {
            return rec_res.status();
        }
    }

    engine_ = std::make_unique<ExecutionEngine>(catalog_.get(), log_mgr_.get());
    is_open_ = true;
    return Status::OK();
}

Status EmberDBInstance::Close() {
    if (!is_open_) {
        return Status::OK();
    }

    if (recovery_mgr_) {
        recovery_mgr_->RecordCleanShutdown();
    }

    if (bpm_) {
        bpm_->FlushAllPages();
    }

    if (log_mgr_) {
        log_mgr_->Close();
    }

    if (disk_mgr_) {
        disk_mgr_->Close();
    }

    engine_.reset();
    recovery_mgr_.reset();
    catalog_.reset();
    log_mgr_.reset();
    bpm_.reset();
    disk_mgr_.reset();

    is_open_ = false;
    return Status::OK();
}

void EmberDBInstance::SimulateCrash() {
    if (!is_open_) {
        return;
    }

    // Flush dirty buffer pool pages and WAL to disk without writing clean checkpoint marker
    if (bpm_) {
        bpm_->FlushAllPages();
    }

    if (log_mgr_) {
        log_mgr_->FlushLogBuffer();
        log_mgr_->Close();
    }

    if (disk_mgr_) {
        disk_mgr_->Close();
    }

    engine_.reset();
    recovery_mgr_.reset();
    catalog_.reset();
    log_mgr_.reset();
    bpm_.reset();
    disk_mgr_.reset();

    is_open_ = false;
}

QueryResult EmberDBInstance::ExecuteQuery(const std::string& sql) {
    if (!is_open_) {
        return QueryResult{false, "Database is not open", {}, {}, 0, 0.0};
    }

    Lexer lexer(sql);
    auto tokens = lexer.Tokenize();
    Parser parser(std::move(tokens));
    auto parse_res = parser.Parse();
    if (!parse_res.ok()) {
        return QueryResult{false, parse_res.status().ToString(), {}, {}, 0, 0.0};
    }

    return engine_->Execute(parse_res->get());
}

} // namespace emberdb
