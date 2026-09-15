# Core static library for database engine components
add_library(forgedb_core STATIC
    common/status.cpp
    common/types.cpp
    catalog/column.cpp
    catalog/schema.cpp
    catalog/catalog.cpp
    storage/page/page.cpp
    storage/page/slotted_page.cpp
    storage/disk/disk_manager.cpp
    storage/buffer/lru_replacer.cpp
    storage/buffer/buffer_pool_manager.cpp
    storage/record/record.cpp
    storage/table/table_heap.cpp
    sql/lexer/token.cpp
    sql/lexer/lexer.cpp
    sql/ast/ast.cpp
    sql/parser/parser.cpp
    execution/expressions/expression_evaluator.cpp
    execution/executor/seq_scan_executor.cpp
    execution/executor/projection_executor.cpp
    execution/executor/sort_executor.cpp
    execution/executor/limit_executor.cpp
    execution/executor/aggregate_executor.cpp
    execution/executor/filter_executor.cpp
    execution/executor/nested_loop_join_executor.cpp
    execution/executor/execution_engine.cpp
)

target_include_directories(forgedb_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(forgedb_core PUBLIC cxx_std_17)

# CLI executable
add_executable(forgedb
    main.cpp
)

target_link_libraries(forgedb
    PRIVATE
        forgedb_core
)

target_compile_features(forgedb PRIVATE cxx_std_17)
