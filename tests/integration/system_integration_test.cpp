#include <catch2/catch.hpp>
#include "emberdb/emberdb.h"
#include <filesystem>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace emberdb;

namespace {
    const std::string SYSTEM_TEST_DIR = "test_system_integration_data";

    void CleanupSystemDir() {
        std::error_code ec;
        std::filesystem::remove_all(SYSTEM_TEST_DIR, ec);
    }
}

TEST_CASE("System Integration: End-to-End Multi-Table Schema, Indexes, Joins, and Aggregations", "[system][integration]") {
    CleanupSystemDir();

    {
        EmberDBInstance db(SYSTEM_TEST_DIR);
        REQUIRE(db.Open().ok());

        // 1. Create tables
        auto r1 = db.ExecuteQuery("CREATE TABLE departments (dept_id INT, dept_name VARCHAR, budget DOUBLE);");
        REQUIRE(r1.success);

        auto r2 = db.ExecuteQuery("CREATE TABLE employees (emp_id INT, emp_name VARCHAR, dept_id INT, salary DOUBLE);");
        REQUIRE(r2.success);

        // 2. Create secondary B+ tree indexes
        auto r3 = db.ExecuteQuery("CREATE INDEX idx_dept ON departments(dept_id);");
        REQUIRE(r3.success);

        auto r4 = db.ExecuteQuery("CREATE INDEX idx_emp ON employees(emp_id);");
        REQUIRE(r4.success);

        // 3. Insert records
        REQUIRE(db.ExecuteQuery("INSERT INTO departments VALUES (10, 'Engineering', 500000.0);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO departments VALUES (20, 'Research', 350000.0);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO departments VALUES (30, 'Marketing', 200000.0);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO departments VALUES (40, 'EmptyDept', 100000.0);").success);

        REQUIRE(db.ExecuteQuery("INSERT INTO employees VALUES (1, 'Alice', 10, 95000.0);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO employees VALUES (2, 'Bob', 10, 80000.0);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO employees VALUES (3, 'Charlie', 20, 110000.0);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO employees VALUES (4, 'David', 20, 75000.0);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO employees VALUES (5, 'Eve', 30, 65000.0);").success);

        // 4. Index-accelerated point queries
        auto idx_res = db.ExecuteQuery("SELECT * FROM employees WHERE emp_id = 3;");
        REQUIRE(idx_res.success);
        REQUIRE(idx_res.rows.size() == 1);
        REQUIRE(idx_res.rows[0].GetValue(idx_res.schema, 1).GetAsVarChar() == "Charlie");

        // 5. INNER JOIN with predicate, projection, and ORDER BY
        auto join_res = db.ExecuteQuery(
            "SELECT employees.emp_name, departments.dept_name, employees.salary "
            "FROM employees INNER JOIN departments ON employees.dept_id = departments.dept_id "
            "WHERE employees.salary >= 80000.0 ORDER BY employees.salary DESC;"
        );
        REQUIRE(join_res.success);
        REQUIRE(join_res.rows.size() == 3);
        REQUIRE(join_res.rows[0].GetValue(join_res.schema, 0).GetAsVarChar() == "Charlie");
        REQUIRE(join_res.rows[0].GetValue(join_res.schema, 1).GetAsVarChar() == "Research");
        REQUIRE(join_res.rows[1].GetValue(join_res.schema, 0).GetAsVarChar() == "Alice");
        REQUIRE(join_res.rows[1].GetValue(join_res.schema, 1).GetAsVarChar() == "Engineering");
        REQUIRE(join_res.rows[2].GetValue(join_res.schema, 0).GetAsVarChar() == "Bob");
        REQUIRE(join_res.rows[2].GetValue(join_res.schema, 1).GetAsVarChar() == "Engineering");

        // 6. LEFT JOIN: verify empty department appears with NULL employee attributes
        auto left_res = db.ExecuteQuery(
            "SELECT departments.dept_name, employees.emp_name "
            "FROM departments LEFT JOIN employees ON departments.dept_id = employees.dept_id "
            "ORDER BY departments.dept_id;"
        );
        REQUIRE(left_res.success);
        REQUIRE(left_res.rows.size() >= 5);
        bool found_empty_dept = false;
        for (const auto& row : left_res.rows) {
            if (row.GetValue(left_res.schema, 0).GetAsVarChar() == "EmptyDept") {
                found_empty_dept = true;
                REQUIRE(row.GetValue(left_res.schema, 1).IsNull());
            }
        }
        REQUIRE(found_empty_dept);

        // 7. Aggregation with GROUP BY: COUNT(*) per department
        auto agg_res = db.ExecuteQuery(
            "SELECT dept_id, COUNT(*) FROM employees GROUP BY dept_id ORDER BY dept_id;"
        );
        REQUIRE(agg_res.success);
        REQUIRE(agg_res.rows.size() == 3); // depts 10, 20, 30
        REQUIRE(agg_res.rows[0].GetValue(agg_res.schema, 0).GetAsInteger() == 10);
        REQUIRE(agg_res.rows[0].GetValue(agg_res.schema, 1).GetAsBigInt() == 2); // Alice & Bob

        // Clean close
        REQUIRE(db.Close().ok());
    }

    // Step 8: Reopen database and verify full persistence across restart
    {
        EmberDBInstance db(SYSTEM_TEST_DIR);
        REQUIRE(db.Open().ok());

        // Verify catalog persisted
        REQUIRE(db.GetCatalog()->HasTable("departments"));
        REQUIRE(db.GetCatalog()->HasTable("employees"));
        REQUIRE(db.GetCatalog()->HasIndex("idx_dept"));
        REQUIRE(db.GetCatalog()->HasIndex("idx_emp"));

        // Verify index-accelerated point lookup works immediately after reopening
        auto q_res = db.ExecuteQuery("SELECT * FROM employees WHERE emp_id = 1;");
        REQUIRE(q_res.success);
        REQUIRE(q_res.rows.size() == 1);
        REQUIRE(q_res.rows[0].GetValue(q_res.schema, 1).GetAsVarChar() == "Alice");

        // Verify join still works after restart
        auto j_res = db.ExecuteQuery(
            "SELECT employees.emp_name, departments.dept_name "
            "FROM employees INNER JOIN departments ON employees.dept_id = departments.dept_id "
            "WHERE employees.emp_id = 5;"
        );
        REQUIRE(j_res.success);
        REQUIRE(j_res.rows.size() == 1);
        REQUIRE(j_res.rows[0].GetValue(j_res.schema, 0).GetAsVarChar() == "Eve");
        REQUIRE(j_res.rows[0].GetValue(j_res.schema, 1).GetAsVarChar() == "Marketing");

        REQUIRE(db.Close().ok());
    }

    CleanupSystemDir();
}

TEST_CASE("System Integration: Crash Recovery and ACID Invariance Under Mixed Workload", "[system][recovery]") {
    CleanupSystemDir();

    // Setup initial data and clean checkpoint
    {
        EmberDBInstance db(SYSTEM_TEST_DIR);
        REQUIRE(db.Open().ok());

        REQUIRE(db.ExecuteQuery("CREATE TABLE bank (acc_id INT, name VARCHAR, balance INT);").success);
        REQUIRE(db.ExecuteQuery("CREATE INDEX idx_bank ON bank(acc_id);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO bank VALUES (1, 'Alice', 1000);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO bank VALUES (2, 'Bob', 2000);").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO bank VALUES (3, 'Charlie', 3000);").success);

        REQUIRE(db.Close().ok());
    }

    // Unclean shutdown simulation: committed txn + uncommitted loser txn without clean checkpoint
    {
        EmberDBInstance db(SYSTEM_TEST_DIR);
        REQUIRE(db.Open().ok());

        // Committed transaction
        REQUIRE(db.ExecuteQuery("BEGIN TRANSACTION;").success);
        REQUIRE(db.ExecuteQuery("UPDATE bank SET balance = 1500 WHERE acc_id = 1;").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO bank VALUES (4, 'David', 4000);").success);
        REQUIRE(db.ExecuteQuery("COMMIT TRANSACTION;").success);

        // Uncommitted transaction (simulates active crash point)
        REQUIRE(db.ExecuteQuery("BEGIN TRANSACTION;").success);
        REQUIRE(db.ExecuteQuery("UPDATE bank SET balance = 9999 WHERE acc_id = 2;").success);
        REQUIRE(db.ExecuteQuery("INSERT INTO bank VALUES (5, 'CrashLoser', 5000);").success);
        REQUIRE(db.ExecuteQuery("DELETE FROM bank WHERE acc_id = 3;").success);

        // Simulate crash: flush pages and WAL but omit clean checkpoint
        db.SimulateCrash();
    }

    // Reopen: recovery manager should automatically detect unclean shutdown, redo committed, undo losers
    {
        EmberDBInstance db(SYSTEM_TEST_DIR);
        REQUIRE(db.Open().ok());

        // Alice (committed): balance was updated to 1500
        auto q_alice = db.ExecuteQuery("SELECT balance FROM bank WHERE acc_id = 1;");
        REQUIRE(q_alice.success);
        REQUIRE(q_alice.rows.size() == 1);
        REQUIRE(q_alice.rows[0].GetValue(q_alice.schema, 0).GetAsInteger() == 1500);

        // David (committed): inserted with balance 4000
        auto q_david = db.ExecuteQuery("SELECT balance FROM bank WHERE acc_id = 4;");
        REQUIRE(q_david.success);
        REQUIRE(q_david.rows.size() == 1);
        REQUIRE(q_david.rows[0].GetValue(q_david.schema, 0).GetAsInteger() == 4000);

        // Bob (loser): balance was NOT committed at 9999, must be restored to 2000
        auto q_bob = db.ExecuteQuery("SELECT balance FROM bank WHERE acc_id = 2;");
        REQUIRE(q_bob.success);
        REQUIRE(q_bob.rows.size() == 1);
        REQUIRE(q_bob.rows[0].GetValue(q_bob.schema, 0).GetAsInteger() == 2000);

        // CrashLoser (loser): must be undone (0 rows)
        auto q_loser = db.ExecuteQuery("SELECT * FROM bank WHERE acc_id = 5;");
        REQUIRE(q_loser.success);
        REQUIRE(q_loser.rows.empty());

        // Charlie (loser deletion): must be resurrected (Charlie with 3000 restored)
        auto q_charlie = db.ExecuteQuery("SELECT balance FROM bank WHERE acc_id = 3;");
        REQUIRE(q_charlie.success);
        REQUIRE(q_charlie.rows.size() == 1);
        REQUIRE(q_charlie.rows[0].GetValue(q_charlie.schema, 0).GetAsInteger() == 3000);

        // Continued operations after recovery: insert, update, select
        REQUIRE(db.ExecuteQuery("INSERT INTO bank VALUES (6, 'Frank', 6000);").success);
        auto q_frank = db.ExecuteQuery("SELECT balance FROM bank WHERE acc_id = 6;");
        REQUIRE(q_frank.success);
        REQUIRE(q_frank.rows.size() == 1);
        REQUIRE(q_frank.rows[0].GetValue(q_frank.schema, 0).GetAsInteger() == 6000);

        REQUIRE(db.Close().ok());
    }

    CleanupSystemDir();
}

TEST_CASE("System Integration: High-Concurrency Stress Test Across Buffer Pool, Catalog, Tables, and Indexes", "[system][concurrency]") {
    CleanupSystemDir();

    EmberDBInstance db(SYSTEM_TEST_DIR);
    REQUIRE(db.Open().ok());

    REQUIRE(db.ExecuteQuery("CREATE TABLE items (id INT, tag VARCHAR, score INT);").success);
    REQUIRE(db.ExecuteQuery("CREATE INDEX idx_items_id ON items(id);").success);

    constexpr int NUM_THREADS = 6;
    constexpr int OPS_PER_THREAD = 60;
    std::atomic<bool> start_signal{false};
    std::atomic<int> success_count{0};
    std::vector<std::thread> workers;

    for (int t = 0; t < NUM_THREADS; ++t) {
        workers.emplace_back([&, t]() {
            while (!start_signal.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            int base_id = t * 1000;
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                int id = base_id + i;
                std::string insert_sql = "INSERT INTO items VALUES (" + std::to_string(id) + ", 'Tag_" +
                                         std::to_string(id) + "', " + std::to_string(i * 10) + ");";
                auto res = db.ExecuteQuery(insert_sql);
                if (res.success) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }

                // Point query via B+ tree index
                std::string select_sql = "SELECT * FROM items WHERE id = " + std::to_string(id) + ";";
                auto s_res = db.ExecuteQuery(select_sql);
                if (s_res.success && s_res.rows.size() == 1) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start_signal.store(true);
    for (auto& w : workers) {
        w.join();
    }

    // Verify all inserts and selects succeeded
    REQUIRE(success_count.load() == NUM_THREADS * OPS_PER_THREAD * 2);

    // Verify total count in relation
    auto count_res = db.ExecuteQuery("SELECT COUNT(*) FROM items;");
    REQUIRE(count_res.success);
    REQUIRE(count_res.rows.size() == 1);
    REQUIRE(count_res.rows[0].GetValue(count_res.schema, 0).GetAsBigInt() == NUM_THREADS * OPS_PER_THREAD);

    REQUIRE(db.Close().ok());
    CleanupSystemDir();
}
