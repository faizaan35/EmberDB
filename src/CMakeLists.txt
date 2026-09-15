# Core static library for database engine components
add_library(forgedb_core STATIC
    common/status.cpp
    common/types.cpp
    catalog/column.cpp
    catalog/schema.cpp
    storage/page/page.cpp
    storage/disk/disk_manager.cpp
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
