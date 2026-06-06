cmake_minimum_required(VERSION 3.16)

project(pq_nas
        VERSION 0.1.0
        LANGUAGES C CXX
)

# -----------------------------------------------------------------------------
# Global standards / warnings
# -----------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings (portable)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
find_package(OpenSSL 3.0 REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(SQLite3 3.39 REQUIRED)

pkg_check_modules(SODIUM REQUIRED libsodium>=1.0.18)
pkg_check_modules(QRENCODE REQUIRED libqrencode>=4.1.1)
pkg_check_modules(JSONCPP REQUIRED jsoncpp>=1.9.5)

# -----------------------------------------------------------------------------
# Core library (default: used by tools/vectors; permissive / frozen-vector friendly)
# -----------------------------------------------------------------------------
add_library(pqnas_core STATIC
        core/qrauth_v4.cc
)

target_include_directories(pqnas_core PUBLIC
        ${CMAKE_SOURCE_DIR}/core
)

target_include_directories(pqnas_core PUBLIC
        ${CMAKE_SOURCE_DIR}/core
        ${JSONCPP_INCLUDE_DIRS}
)

target_include_directories(pqnas_core PRIVATE
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_core PRIVATE
        ${SODIUM_CFLAGS_OTHER}
        ${JSONCPP_CFLAGS_OTHER}
)

target_link_libraries(pqnas_core PUBLIC
        ${JSONCPP_LIBRARIES}
)

target_link_libraries(pqnas_core PRIVATE
        ${SODIUM_LIBRARIES}
        OpenSSL::Crypto
)

# -----------------------------------------------------------------------------
# Core library (server strict mode: enables time enforcement in qrauth_v4.c)
# -----------------------------------------------------------------------------
add_library(pqnas_core_server STATIC
        core/qrauth_v4.cc
)

target_include_directories(pqnas_core_server PUBLIC
        ${CMAKE_SOURCE_DIR}/core
        ${JSONCPP_INCLUDE_DIRS}
)

target_include_directories(pqnas_core_server PRIVATE
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_core_server PRIVATE
        ${SODIUM_CFLAGS_OTHER}
        ${JSONCPP_CFLAGS_OTHER}
)

# Enforce req.exp and proof ts freshness in server builds only.
target_compile_definitions(pqnas_core_server PRIVATE
        QR_V4_ENFORCE_TIME=1
        QR_V4_MAX_SKEW_SEC=300
)

target_link_libraries(pqnas_core_server PUBLIC
        ${JSONCPP_LIBRARIES}
)

target_link_libraries(pqnas_core_server PRIVATE
        ${SODIUM_LIBRARIES}
        OpenSSL::Crypto
)

# -----------------------------------------------------------------------------
# Shared v4 verifier library (single source of truth for server + tools)
# -----------------------------------------------------------------------------
add_library(pqnas_verify_v4 STATIC
        server/src/v4_verify_shared.cc
        server/src/verify_v4_crypto.cc
        server/src/pqnas_util.cc
        server/src/authz.cc
        server/src/users_registry.cpp
        server/src/user_quota.cc
)

target_include_directories(pqnas_verify_v4 PUBLIC
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_verify_v4 PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

# IMPORTANT:
# - Server must get strict time enforcement in core/qrauth_v4.cc
# - Tools/vectors should remain permissive to avoid breaking frozen vectors.
# Therefore pqnas_verify_v4 links against pqnas_core_server.
target_link_libraries(pqnas_verify_v4 PUBLIC
        pqnas_core_server
        ${SODIUM_LIBRARIES}
        OpenSSL::Crypto
        dl
)

# -----------------------------------------------------------------------------
# Vendored ML-KEM-768 backend used by DNA pqcore wrapper inside pqnas_server
# -----------------------------------------------------------------------------
add_library(dnanexus_mlkem_native_obj OBJECT
        server/third_party/mlkem-native/mlkem/mlkem_native.c
)

target_include_directories(dnanexus_mlkem_native_obj PRIVATE
        ${CMAKE_SOURCE_DIR}/server/third_party/mlkem-native
        ${CMAKE_SOURCE_DIR}/core/dna_pqcore
)

target_compile_definitions(dnanexus_mlkem_native_obj PRIVATE
        MLK_CONFIG_FILE=\"dna_mlkem_native_config_768.h\"
)

# -----------------------------------------------------------------------------
# DNA-owned ML-KEM wrapper library used by pqnas_server share code
# -----------------------------------------------------------------------------
add_library(pqnas_dna_pqcore_mlkem STATIC
        core/dna_pqcore/dna_mlkem768_backend.cpp
        core/dna_pqcore/internal/dna_mlkem768_provider_native.cpp
        $<TARGET_OBJECTS:dnanexus_mlkem_native_obj>
)

target_include_directories(pqnas_dna_pqcore_mlkem PUBLIC
        ${CMAKE_SOURCE_DIR}/core/dna_pqcore
)

target_include_directories(pqnas_dna_pqcore_mlkem PRIVATE
        ${CMAKE_SOURCE_DIR}/server/third_party/mlkem-native
)

target_link_libraries(pqnas_dna_pqcore_mlkem PRIVATE
        OpenSSL::Crypto
)

# -----------------------------------------------------------------------------
# Server (C++ / httplib)
# -----------------------------------------------------------------------------
add_executable(pqnas_server
        server/src/main.cpp
        server/src/user_storage_migration.h
        server/src/user_storage_migration.cpp

        server/src/storage/snapshots/snapshot_provider_btrfs.cc
        server/src/storage/snapshots/snapshot_naming.cc
        server/src/storage/snapshots/snapshot_scheduler.cc

        server/src/routes_v5.cc
        server/src/password_credentials.cpp
        server/src/session_cookie.cc
        server/src/policy.cc
        server/src/audit_log.cc
        server/src/allowlist.cc
        server/src/users_registry.cpp
        server/src/system_metrics.cc
        server/src/storage_info.cc
        server/src/drive_health.cc
        server/src/drive_health_monitor.cc
        server/src/routes_drive_locate.cpp
        server/src/routes_drive_locate.h
        server/src/share_links.cc
        server/src/activity_log.cpp
        server/src/routes_activity.cpp
        server/src/people_contacts.cpp
        server/src/routes_people.cpp
        server/src/file_annotations.cpp
        server/src/routes_file_annotations.cpp
        server/src/file_locks.cpp
        server/src/routes_file_locks.cpp
        server/src/static_serve.cc
        server/src/verify_login_common.cc
        server/src/storage_resolver.cpp
        server/src/file_favorites.cc
        server/src/file_location_index.cpp
        server/src/path_lock_manager.cpp
        server/src/storage_pools.cc
        server/src/app_tokens.cpp
        server/src/app_pairing.cpp
        server/src/share_pq_v1.cpp
        server/src/share_pq_crypto_v1.cpp
        server/src/share_pq_mlkem_v1.cpp
        server/src/share_pq_mlkem_v1.h
        server/src/routes_workspaces_files.cpp
        server/src/workspaces.cpp
        server/src/workspace_external_invites.cpp
        server/src/workspace_external_sessions.cpp
        server/src/routes_workspace_external_invites.cpp
        server/src/routes_workspace_external_sessions.cpp
        server/src/workspace_access_shared.cpp
        server/src/runtime_paths.cpp
        server/src/backups/system_backup_worker.cpp
        server/src/backups/system_backup_worker.h
        server/src/backups/system_backup_routes.cpp
        server/src/backups/system_backup_routes.h
        server/src/updates/update_center_routes.cpp
        server/src/updates/update_center_routes.h
        server/src/file_versions.cpp
        server/src/file_versions_restore.cpp
        server/src/file_versions_read.cpp
        server/src/gallery_meta.cpp
        server/src/image_embedded_meta.cpp
        server/src/trash_index.cpp
        server/src/trash_service.cpp
        server/src/trash_routes.cpp
        server/src/dropzone_routes.cpp
        server/src/dropzone_index.cpp
        server/src/echo_stack_routes.cpp
        server/src/achievements.cpp
        server/src/achievements.h
        server/src/circle_stack_routes.cpp
        server/src/circle_stack_memory_nodes.cpp
        server/src/federation/pqnas_nodus_client.cpp
        server/src/federation/pqnas_nodus_client.h
        server/src/federation/circle_federation_event.cpp
        server/src/federation/circle_federation_event.h
        server/src/federation/circle_federation_signing.cpp
        server/src/federation/circle_federation_signing.h
        server/src/federation/circle_federation_outbox.cpp
        server/src/federation/circle_federation_outbox.h
        server/src/federation/circle_federation_outbox_worker.cpp
        server/src/federation/circle_federation_outbox_worker.h
        server/src/federation/circle_federation_inbox.cpp
        server/src/federation/circle_federation_inbox.h
        server/src/federation/circle_federation_remote_feed.cpp
        server/src/federation/circle_federation_remote_feed.h
        server/src/routes_circle_nodus_research.cpp
        server/src/routes_circle_nodus_research.h
        server/src/echo_stack_index.cpp
        server/src/echo_stack_text_extract.cpp
        server/src/echo_stack_content_index.cpp
        server/src/echo_stack_search_routes.cpp
        server/src/gallery_albums.cpp
        server/src/gallery_albums_routes.cpp

)

# Treat warnings as errors for the server (security-sensitive)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pqnas_server PRIVATE -Werror)
endif()

target_include_directories(pqnas_server PRIVATE
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/core/dna_pqcore
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${CMAKE_SOURCE_DIR}/server/third_party/mlkem-native
        ${SODIUM_INCLUDE_DIRS}
        ${QRENCODE_INCLUDE_DIRS}
)

target_compile_options(pqnas_server PRIVATE
        ${SODIUM_CFLAGS_OTHER}
        ${QRENCODE_CFLAGS_OTHER}
)

target_compile_definitions(pqnas_server PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(pqnas_server PRIVATE
        pqnas_verify_v4
        pqnas_dna_pqcore_mlkem
        ${QRENCODE_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
        SQLite::SQLite3
        Threads::Threads
        dl
)

set_target_properties(pqnas_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set(PQNAS_DNA_LIB_SRC "${CMAKE_SOURCE_DIR}/server/third_party/dna/lib/linux/x64/libdna_lib.so")

add_custom_command(TARGET pqnas_server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PQNAS_DNA_LIB_SRC}"
        "$<TARGET_FILE_DIR:pqnas_server>/libdna_lib.so"
)

# -----------------------------------------------------------------------------
# Tool: verify_vectors
# -----------------------------------------------------------------------------
add_executable(verify_vectors
        tests/vectors/verify_vectors.c
)

target_include_directories(verify_vectors PRIVATE
        ${CMAKE_SOURCE_DIR}/core
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(verify_vectors PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

# IMPORTANT: verify_vectors stays linked to permissive core to keep frozen vectors working.
target_link_libraries(verify_vectors PRIVATE
        pqnas_core
        ${SODIUM_LIBRARIES}
        OpenSSL::Crypto
)

set_target_properties(verify_vectors PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_verify_vectors
        COMMAND "${CMAKE_BINARY_DIR}/bin/verify_vectors"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS verify_vectors
)

# -----------------------------------------------------------------------------
# Tool: verify_v4_vectors (JSON protocol freeze; uses shared verifier)
# -----------------------------------------------------------------------------
add_executable(verify_v4_vectors
        tests/v4_vectors/verify_v4_vectors.cpp
)

target_link_libraries(verify_v4_vectors PRIVATE
        pqnas_verify_v4
)

set_target_properties(verify_v4_vectors PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_compile_options(verify_v4_vectors PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_include_directories(verify_v4_vectors PRIVATE
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

add_custom_command(TARGET verify_v4_vectors POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PQNAS_DNA_LIB_SRC}"
        "$<TARGET_FILE_DIR:verify_v4_vectors>/libdna_lib.so"
)

# -----------------------------------------------------------------------------
# Tool: verify_v5_vectors (v5 glue checks; derives/compares k == st_hash_b64)
# -----------------------------------------------------------------------------
add_executable(verify_v5_vectors
        tests/v5_vectors/verify_v5_vectors.cpp
)

target_link_libraries(verify_v5_vectors PRIVATE
        pqnas_verify_v4
)

set_target_properties(verify_v5_vectors PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_compile_options(verify_v5_vectors PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_include_directories(verify_v5_vectors PRIVATE
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

add_custom_command(TARGET verify_v5_vectors POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PQNAS_DNA_LIB_SRC}"
        "$<TARGET_FILE_DIR:verify_v5_vectors>/libdna_lib.so"
)

# -----------------------------------------------------------------------------
# Tool: test_admin_cookie (regression test for cookie+policy admin checks)
# -----------------------------------------------------------------------------
add_executable(test_admin_cookie
        tests/cookies/test_admin_cookie.cpp
        server/src/authz.cc
        server/src/allowlist.cc
        server/src/session_cookie.cc
        server/src/pqnas_util.cc
        server/src/users_registry.cpp
)

target_include_directories(test_admin_cookie PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${CMAKE_SOURCE_DIR}/core
        ${SODIUM_INCLUDE_DIRS}
)

target_link_libraries(test_admin_cookie PRIVATE
        OpenSSL::Crypto
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_admin_cookie PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_cookie
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_cookie"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_cookie
)

# -----------------------------------------------------------------------------
# Tool: pqnas_keygen
# -----------------------------------------------------------------------------
add_executable(pqnas_keygen
        tools/keygen.c
)

target_include_directories(pqnas_keygen PRIVATE
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_keygen PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(pqnas_keygen PRIVATE
        ${SODIUM_LIBRARIES}
)

set_target_properties(pqnas_keygen PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)



# -----------------------------------------------------------------------------
# Bundled Nodus client CLI used by Circle Stack federation
#
# Build Nodus as an isolated sub-build instead of add_subdirectory().
# Reason: PQ-NAS uses strict server-wide warning/error flags, while the vendored
# Nodus tree is a separate C project with its own compile assumptions.
#
# Developer note:
# - Keep this out of the default ALL build by default. The bundled Nodus CLI
#   changes rarely and is slow to rebuild during normal pqnas_server work.
# - Build it explicitly with:
#       cmake --build build --target pqnas_nodus_cli -j
# - Or include it in every default build with:
#       cmake -S . -B build -DPQNAS_BUILD_BUNDLED_NODUS_CLI=ON
# -----------------------------------------------------------------------------
option(PQNAS_BUILD_BUNDLED_NODUS_CLI
       "Build bundled Nodus CLI as part of the default ALL target"
       OFF)

set(PQNAS_BUNDLED_NODUS_DIR "${CMAKE_SOURCE_DIR}/third_party/nodus")
set(PQNAS_BUNDLED_NODUS_BUILD_DIR "${CMAKE_BINARY_DIR}/third_party/nodus-standalone")
set(PQNAS_BUNDLED_NODUS_CLI "${CMAKE_BINARY_DIR}/bin/nodus-cli")

if (EXISTS "${PQNAS_BUNDLED_NODUS_DIR}/CMakeLists.txt")
    set(PQNAS_NODUS_CLI_ALL_ARG)
    if (PQNAS_BUILD_BUNDLED_NODUS_CLI)
        set(PQNAS_NODUS_CLI_ALL_ARG ALL)
    endif()

    add_custom_target(nodus-cli ${PQNAS_NODUS_CLI_ALL_ARG}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${CMAKE_COMMAND}
            -S "${PQNAS_BUNDLED_NODUS_DIR}"
            -B "${PQNAS_BUNDLED_NODUS_BUILD_DIR}"
            -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}/bin
            -DCMAKE_C_FLAGS=-D_GNU_SOURCE
        COMMAND ${CMAKE_COMMAND}
            --build "${PQNAS_BUNDLED_NODUS_BUILD_DIR}"
            --target nodus-cli
            --parallel
        BYPRODUCTS "${PQNAS_BUNDLED_NODUS_CLI}"
        COMMENT "Building bundled Nodus CLI"
        VERBATIM
    )

    add_custom_target(pqnas_nodus_cli
        DEPENDS nodus-cli
    )
else()
    message(WARNING "Bundled Nodus tree not found at ${PQNAS_BUNDLED_NODUS_DIR}; nodus-cli will not be built")
endif()




# -----------------------------------------------------------------------------
# Run targets
# -----------------------------------------------------------------------------
add_custom_target(run_verify_v4_vectors
        COMMAND "${CMAKE_BINARY_DIR}/bin/verify_v4_vectors" "${CMAKE_SOURCE_DIR}/tests/v4_vectors/vectors.json"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS verify_v4_vectors
)

add_custom_target(run_verify_v5_vectors
        COMMAND "${CMAKE_BINARY_DIR}/bin/verify_v5_vectors" "${CMAKE_SOURCE_DIR}/tests/v5_vectors/v5_vectors.json"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS verify_v5_vectors
)


# --- Circle Stack Nodus federation research sources ---
target_sources(pqnas_server PRIVATE
    server/src/federation/pqnas_nodus_client.cpp
    server/src/federation/circle_federation_event.cpp
)

