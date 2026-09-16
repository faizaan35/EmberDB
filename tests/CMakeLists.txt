add_executable(emberdb_tests
    test_main.cpp
    unit/common_test.cpp
    unit/types_test.cpp
    unit/disk_manager_test.cpp
    unit/record_test.cpp
    unit/slotted_page_test.cpp
    storage/persistence_test.cpp
    storage/table_heap_test.cpp
    catalog/catalog_test.cpp
    integration/table_persistence_test.cpp
    sql/lexer_test.cpp
    sql/parser_test.cpp
    execution/execution_engine_test.cpp
    execution/query_features_test.cpp
    execution/join_test.cpp
    storage/buffer_pool_test.cpp
    index/b_plus_tree_test.cpp
    index/index_persistence_test.cpp
)

target_include_directories(emberdb_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(emberdb_tests
    PRIVATE
        emberdb_core
)

target_compile_features(emberdb_tests PRIVATE cxx_std_17)

# Register with CTest
add_test(NAME emberdb_unit_tests COMMAND emberdb_tests)
