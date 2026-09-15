# EmberDB Architectural Decision Records (ADRs)

## ADR-001: Selection of C++17 Standard

* **Context**: Modern database engines require low-level memory control, predictable performance, and strong type safety.
* **Decision**: Adopt ISO C++17 as the core language standard for EmberDB.
* **Why**: C++17 provides essential vocabulary types (`std::variant`, `std::optional`, `std::string_view`, structured bindings, `std::shared_mutex`) while enjoying universal compiler support across Windows (MSVC/MinGW GCC) and Linux (GCC/Clang).
* **Alternatives Considered**:
  * C++14: Lacks `std::variant` and `std::optional`, necessitating external dependencies.
  * C++20: Module and coroutine support varies across compilers and toolchains.
* **Tradeoffs**: Requires modern compiler toolchains; manual memory ownership requires rigorous RAII hygiene.

---

## ADR-002: Fixed 4096-Byte Slotted Page Format

* **Context**: Database disk persistence requires a structured layout to store tuples of varying lengths while maintaining stable row identifiers.
* **Decision**: Standardize on a fixed page size of 4096 bytes (4 KB) with a bidirectional slotted-page layout.
* **Why**: 4 KB matches the standard virtual memory page size of modern operating systems and hardware SSD block sectors, minimizing read/write amplification. Slotted pages allow records to move during fragmentation cleanup without altering external Record Identifiers (RIDs).
* **Alternatives Considered**:
  * Fixed-length records per page: Wasteful for variable-length strings like `VARCHAR`.
  * Append-only log-structured pages: Complex compaction and slower point-lookup reads.
* **Tradeoffs**: Records larger than ~4000 bytes require overflow/spill pages.

---

## ADR-003: Catch2 Testing Framework

* **Context**: Subsystem verification must be automated, rigorous, and capable of unit and integration test suites without external network dependencies during compilation.
* **Decision**: Adopt Catch2 (single-header distribution) as the primary test framework.
* **Why**: Catch2 enables natural BDD-style assertions (`REQUIRE`, `CHECK`), structured test sections (`SECTION`), clear failure diffs, zero runtime dependencies, and seamless CTest integration. The single-header distribution ensures full offline reproducibility.
* **Alternatives Considered**:
  * GoogleTest: Requires additional CMake target compilation and external download.
  * Custom assert macros: Lacks structured failure reporting, test filtering, and fixture management.
* **Tradeoffs**: Longer single-file compilation time for the translation unit that defines `CATCH_CONFIG_MAIN`.

---

## ADR-004: Explicit Status and Result<T> Error Handling

* **Context**: Database internals experience expected errors (e.g. disk full, table not found, syntax error) that should be handled gracefully without silent corruption or unhandled crashes.
* **Decision**: Implement an explicit `Status` enum/class and a monad-like `Result<T>` template for non-exceptional error propagation, reserving C++ exceptions for unexpected invariant violations.
* **Why**: Aligns with modern systems programming (Google Abseil `absl::Status`, Rust `Result<T, E>`). Forces callers to explicitly check outcomes, avoiding hidden control flow paths and overhead from exception unwinding in performance-critical execution loops.
* **Alternatives Considered**:
  * Pure C++ exception hierarchy (`std::runtime_error`): Exception unwinding can obscure error origins and incurs performance penalties.
  * C-style error codes: Vulnerable to ignored return values and lack context strings.
* **Tradeoffs**: Slightly more verbose return-value checking at function call sites.
