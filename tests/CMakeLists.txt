add_executable(forgedb_tests
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
)

target_include_directories(forgedb_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(forgedb_tests
    PRIVATE
        forgedb_core
)

target_compile_features(forgedb_tests PRIVATE cxx_std_17)

# Register with CTest
add_test(NAME forgedb_unit_tests COMMAND forgedb_tests)
