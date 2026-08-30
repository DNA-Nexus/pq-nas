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
# Cluster identity validation
# -----------------------------------------------------------------------------
add_library(pqnas_cluster_identity_validation STATIC
        server/src/cluster/cluster_identity_validation.cpp
        server/src/cluster/cluster_identity_validation.h
)

target_include_directories(pqnas_cluster_identity_validation PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

# Security-sensitive canonicalization should remain warning-clean.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_cluster_identity_validation
        PRIVATE
        -Werror
    )
endif()


# -----------------------------------------------------------------------------
# Node enrollment token primitives
# -----------------------------------------------------------------------------
add_library(pqnas_node_enrollment_token STATIC
        server/src/cluster/node_enrollment_token.cpp
        server/src/cluster/node_enrollment_token.h
)

target_include_directories(pqnas_node_enrollment_token PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_node_enrollment_token PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_enrollment_token
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_node_enrollment_token PUBLIC
        ${SODIUM_LIBRARIES}
)


# -----------------------------------------------------------------------------
# Storage handoff one-time ticket store
# -----------------------------------------------------------------------------
add_library(pqnas_storage_handoff_ticket_store STATIC
        server/src/cluster/storage_handoff_ticket_store.cpp
        server/src/cluster/storage_handoff_ticket_store.h
)

target_include_directories(pqnas_storage_handoff_ticket_store PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_storage_handoff_ticket_store PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_handoff_ticket_store
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_handoff_ticket_store PUBLIC
        pqnas_cluster_identity_validation
        ${SODIUM_LIBRARIES}
        Threads::Threads
)


# -----------------------------------------------------------------------------
# Internal Storage handoff routes
# -----------------------------------------------------------------------------
add_library(pqnas_internal_storage_handoff_routes STATIC
        server/src/routes/routes_internal_storage_handoff.cpp
        server/src/routes/routes_internal_storage_handoff.h
)

target_include_directories(pqnas_internal_storage_handoff_routes PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(pqnas_internal_storage_handoff_routes PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_internal_storage_handoff_routes
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_internal_storage_handoff_routes PUBLIC
        pqnas_storage_handoff_ticket_store
)


# -----------------------------------------------------------------------------
# Persistent node enrollment registry
# -----------------------------------------------------------------------------
add_library(pqnas_node_enrollment_registry STATIC
        server/src/cluster/node_enrollment_registry.cpp
        server/src/cluster/node_enrollment_registry.h
)

target_include_directories(pqnas_node_enrollment_registry PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_node_enrollment_registry PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_enrollment_registry
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_node_enrollment_registry PUBLIC
        pqnas_cluster_identity_validation
        pqnas_node_enrollment_token
        ${SODIUM_LIBRARIES}
)


# -----------------------------------------------------------------------------
# Persistent per-node credential registry
# -----------------------------------------------------------------------------
add_library(pqnas_node_credential_registry STATIC
        server/src/cluster/node_credential_registry.cpp
        server/src/cluster/node_credential_registry.h
)

target_include_directories(pqnas_node_credential_registry PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_node_credential_registry PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_credential_registry
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_node_credential_registry PUBLIC
        pqnas_cluster_identity_validation
        ${SODIUM_LIBRARIES}
)


# -----------------------------------------------------------------------------
# Director Node credential rotation orchestration
# -----------------------------------------------------------------------------
add_library(pqnas_node_credential_rotation_service STATIC
        server/src/cluster/node_credential_rotation_service.cpp
        server/src/cluster/node_credential_rotation_service.h
)

target_include_directories(
        pqnas_node_credential_rotation_service
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_credential_rotation_service
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_node_credential_rotation_service
        PUBLIC
        pqnas_cluster_identity_validation
        pqnas_node_credential_registry
)


# -----------------------------------------------------------------------------
# Cluster-safe Node deletion lifecycle
# -----------------------------------------------------------------------------
add_library(pqnas_node_deletion_service STATIC
        server/src/cluster/node_deletion_service.cpp
        server/src/cluster/node_deletion_service.h
)

target_include_directories(pqnas_node_deletion_service PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_deletion_service
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_node_deletion_service PUBLIC
        pqnas_cluster_identity_validation
        pqnas_node_credential_registry
        pqnas_node_enrollment_registry
)


# -----------------------------------------------------------------------------
# Storage node credential rotation runtime
# -----------------------------------------------------------------------------
add_library(pqnas_storage_node_credential_runtime STATIC
        server/src/cluster/storage_node_credential_runtime.cpp
        server/src/cluster/storage_node_credential_runtime.h
)

target_include_directories(
        pqnas_storage_node_credential_runtime
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_node_credential_runtime
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_storage_node_credential_runtime
        PUBLIC
        Threads::Threads
)


# -----------------------------------------------------------------------------
# Internal Storage Node credential rotation routes
# -----------------------------------------------------------------------------
add_library(pqnas_internal_node_credential_rotation_routes STATIC
        server/src/routes/routes_internal_node_credential_rotation.cpp
        server/src/routes/routes_internal_node_credential_rotation.h
)

target_include_directories(
        pqnas_internal_node_credential_rotation_routes
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(
        pqnas_internal_node_credential_rotation_routes
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_internal_node_credential_rotation_routes
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_internal_node_credential_rotation_routes
        PUBLIC
        pqnas_storage_node_credential_runtime
)


# -----------------------------------------------------------------------------
# Storage node provisioning store
# -----------------------------------------------------------------------------
add_library(pqnas_storage_node_provisioning_store STATIC
        server/src/cluster/storage_node_provisioning_store.cpp
        server/src/cluster/storage_node_provisioning_store.h
)

target_include_directories(
        pqnas_storage_node_provisioning_store
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(
        pqnas_storage_node_provisioning_store
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        pqnas_storage_node_provisioning_store
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_node_provisioning_store
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_storage_node_provisioning_store
        PUBLIC
        pqnas_cluster_identity_validation
        ${SODIUM_LIBRARIES}
)


# -----------------------------------------------------------------------------
# Storage node enrollment client
# -----------------------------------------------------------------------------
add_library(pqnas_storage_node_enrollment_client STATIC
        server/src/cluster/storage_node_enrollment_client.cpp
        server/src/cluster/storage_node_enrollment_client.h
)

target_include_directories(
        pqnas_storage_node_enrollment_client
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(
        pqnas_storage_node_enrollment_client
        PRIVATE
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        pqnas_storage_node_enrollment_client
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_node_enrollment_client
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_storage_node_enrollment_client
        PUBLIC
        pqnas_storage_node_provisioning_store
        pqnas_node_enrollment_token
        ${SODIUM_LIBRARIES}
)


# -----------------------------------------------------------------------------
# Storage node enrollment HTTPS transport
# -----------------------------------------------------------------------------
add_library(pqnas_storage_node_enrollment_https STATIC
        server/src/cluster/storage_node_enrollment_https.cpp
        server/src/cluster/storage_node_enrollment_https.h
)

target_include_directories(
        pqnas_storage_node_enrollment_https
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(
        pqnas_storage_node_enrollment_https
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_compile_definitions(
        pqnas_storage_node_enrollment_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_node_enrollment_https
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_storage_node_enrollment_https
        PUBLIC
        pqnas_storage_node_enrollment_client
        OpenSSL::SSL
        OpenSSL::Crypto
)


# -----------------------------------------------------------------------------
# Storage node enrollment runtime
# -----------------------------------------------------------------------------
add_library(pqnas_storage_node_enrollment_runtime STATIC
        server/src/cluster/storage_node_enrollment_runtime.cpp
        server/src/cluster/storage_node_enrollment_runtime.h
)

target_include_directories(
        pqnas_storage_node_enrollment_runtime
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(
        pqnas_storage_node_enrollment_runtime
        PRIVATE
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        pqnas_storage_node_enrollment_runtime
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_node_enrollment_runtime
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_storage_node_enrollment_runtime
        PUBLIC
        pqnas_storage_node_enrollment_client
        pqnas_node_enrollment_token
        ${SODIUM_LIBRARIES}
)


# -----------------------------------------------------------------------------
# Node enrollment service
# -----------------------------------------------------------------------------
add_library(pqnas_node_enrollment_service STATIC
        server/src/cluster/node_enrollment_service.cpp
        server/src/cluster/node_enrollment_service.h
)

target_include_directories(pqnas_node_enrollment_service PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_node_enrollment_service PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_enrollment_service
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_node_enrollment_service PUBLIC
        pqnas_node_enrollment_registry
        pqnas_node_enrollment_token
        ${SODIUM_LIBRARIES}
)


# -----------------------------------------------------------------------------
# Node enrollment consumption service
# -----------------------------------------------------------------------------
add_library(pqnas_node_enrollment_consumption_service STATIC
        server/src/cluster/node_enrollment_consumption_service.cpp
        server/src/cluster/node_enrollment_consumption_service.h
)

target_include_directories(
        pqnas_node_enrollment_consumption_service
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_enrollment_consumption_service
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_node_enrollment_consumption_service
        PUBLIC
        pqnas_cluster_identity_validation
        pqnas_node_credential_registry
        pqnas_node_enrollment_registry
        pqnas_node_enrollment_token
)


# -----------------------------------------------------------------------------
# Node enrollment bootstrap route
# -----------------------------------------------------------------------------
add_library(pqnas_node_enrollment_bootstrap_routes STATIC
        server/src/routes/routes_node_enrollment_bootstrap.cpp
        server/src/routes/routes_node_enrollment_bootstrap.h
)

target_include_directories(
        pqnas_node_enrollment_bootstrap_routes
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(
        pqnas_node_enrollment_bootstrap_routes
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_enrollment_bootstrap_routes
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_node_enrollment_bootstrap_routes
        PUBLIC
        pqnas_node_enrollment_consumption_service
)


# -----------------------------------------------------------------------------
# Admin node enrollment routes
# -----------------------------------------------------------------------------
add_library(pqnas_admin_node_enrollment_routes STATIC
        server/src/routes/routes_admin_node_enrollments.cpp
        server/src/routes/routes_admin_node_enrollments.h
)

target_include_directories(pqnas_admin_node_enrollment_routes PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(pqnas_admin_node_enrollment_routes PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_admin_node_enrollment_routes
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_admin_node_enrollment_routes PUBLIC
        pqnas_node_enrollment_service
)


# -----------------------------------------------------------------------------
# Server (C++ / httplib)
# -----------------------------------------------------------------------------
add_executable(pqnas_server
        server/src/main.cpp
        server/src/cluster/server_role.cpp
        server/src/cluster/server_role.h
        server/src/cluster/internal_auth.cpp
        server/src/cluster/internal_auth.h
        server/src/cluster/node_status.cpp
        server/src/cluster/node_status.h
        server/src/cluster/node_registry.cpp
        server/src/cluster/node_registry.h
        server/src/cluster/node_heartbeat_worker.cpp
        server/src/cluster/node_heartbeat_worker.h
        server/src/cluster/node_heartbeat_https.cpp
        server/src/cluster/node_heartbeat_https.h
        server/src/routes/routes_internal_node_status.cpp
        server/src/routes/routes_internal_node_status.h
        server/src/routes/routes_admin_cluster_nodes.cpp
        server/src/routes/routes_admin_cluster_nodes.h
        server/src/routes/routes_storage_raid.cpp
        server/src/routes/routes_storage_raid.h
        server/src/routes/routes_admin_storage_tiering.cpp
        server/src/routes/routes_admin_storage_tiering.h
        server/src/routes/routes_service_notices.cpp
        server/src/routes/routes_service_notices.h
        server/src/routes/routes_admin_user_lifecycle.cpp
        server/src/routes/routes_admin_user_lifecycle.h
        server/src/routes/routes_admin_user_status.cpp
        server/src/routes/routes_admin_user_status.h
        server/src/routes/routes_admin_user_storage_preview.cpp
        server/src/routes/routes_admin_user_storage_preview.h
        server/src/routes/routes_admin_user_storage.cpp
        server/src/routes/routes_admin_user_storage.h
        server/src/routes/routes_auth_debug_approvals.cpp
        server/src/routes/routes_auth_debug_approvals.h
        server/src/routes/routes_admin_approvals_ui.cpp
        server/src/routes/routes_admin_approvals_ui.h
        server/src/routes/routes_admin_api_explorer.cpp
        server/src/routes/routes_admin_api_explorer.h
        server/src/routes/routes_admin_users_overview.cpp
        server/src/routes/routes_admin_users_overview.h
        server/src/routes/routes_admin_user_storage_jobs.cpp
        server/src/routes/routes_admin_user_storage_jobs.h
        server/src/routes/routes_admin_user_profile.cpp
        server/src/routes/routes_admin_user_profile.h
        server/src/routes/routes_user_avatars.cpp
        server/src/routes/routes_user_avatars.h
        server/src/routes/routes_apps_manage.cpp
        server/src/routes/routes_apps_manage.h
        server/src/routes/routes_apps_public.cpp
        server/src/routes/routes_apps_public.h
        server/src/routes/routes_core_ui_shell.cpp
        server/src/routes/routes_core_ui_shell.h
        server/src/routes/routes_admin_audit_read.cpp
        server/src/routes/routes_admin_audit_read.h
        server/src/routes/routes_admin_audit_rotate.cpp
        server/src/routes/routes_admin_audit_rotate.h
        server/src/routes/routes_admin_audit_retention.cpp
        server/src/routes/routes_admin_audit_retention.h
        server/src/routes/routes_snapshots_browse.cpp
        server/src/routes/routes_snapshots_browse.h
        server/src/routes/routes_snapshots_create.cpp
        server/src/routes/routes_snapshots_create.h
        server/src/routes/routes_snapshots_restore.cpp
        server/src/routes/routes_snapshots_restore.h
        server/src/routes/routes_uploads_chunked.cpp
        server/src/routes/routes_uploads_chunked.h
        server/src/routes/routes_file_versions_archive_blob.cpp
        server/src/routes/routes_file_versions_archive_blob.h
        server/src/routes/routes_file_versions_read.cpp
        server/src/routes/routes_file_versions_read.h
        server/src/routes/routes_file_versions_manage.cpp
        server/src/routes/routes_file_versions_manage.h
        server/src/routes/routes_file_versions_restore.cpp
        server/src/routes/routes_file_versions_restore.h
        server/src/user_storage_migration.h
        server/src/user_storage_migration.cpp

        server/src/storage/snapshots/snapshot_provider_btrfs.cc
        server/src/storage/snapshots/snapshot_naming.cc
        server/src/storage/snapshots/snapshot_scheduler.cc

        server/src/routes/routes_v5.cc
        server/src/interactive_user_login_policy.cc
        server/src/password_credentials.cpp
        server/src/opaque_credentials.cpp
        server/src/durable_file.cpp
        server/src/opaque_backend_status.cpp
        server/src/opaque_helper_client.cpp
        server/src/dna_identity_generator.cpp
        server/src/session_cookie.cc
        server/src/policy.cc
        server/src/audit_log.cc
        server/src/allowlist.cc
        server/src/users_registry.cpp
        server/src/system_metrics.cc
        server/src/storage_info.cc
        server/src/drive_health.cc
        server/src/drive_health_monitor.cc
        server/src/routes/routes_drive_locate.cpp
        server/src/routes/routes_drive_locate.h
        server/src/share_links.cc
        server/src/activity_log.cpp
        server/src/routes/routes_activity.cpp
        server/src/people_contacts.cpp
        server/src/routes/routes_people.cpp
        server/src/notepad_store.cpp
        server/src/notepad_store.h
        server/src/routes/routes_notepad.cpp
        server/src/routes/routes_notepad.h
        server/src/file_annotations.cpp
        server/src/routes/routes_file_annotations.cpp
        server/src/file_locks.cpp
        server/src/routes/routes_file_locks.cpp
        server/src/static_serve.cc
        server/src/verify_login_common.cc
        server/src/storage_resolver.cpp
        server/src/file_favorites.cc
        server/src/file_location_index.cpp
        server/src/path_lock_manager.cpp
        server/src/storage_pools.cc
        server/src/service_notices.cpp
        server/src/service_notices.h
        server/src/app_tokens.cpp
        server/src/app_pairing.cpp
        server/src/share_pq_v1.cpp
        server/src/share_pq_crypto_v1.cpp
        server/src/share_pq_mlkem_v1.cpp
        server/src/share_pq_mlkem_v1.h
        server/src/routes/routes_workspaces_files.cpp
        server/src/workspaces.cpp
        server/src/workspace_messages.cpp
        server/src/workspace_links.cpp
        server/src/routes/routes_workspace_links.cpp
        server/src/workspace_external_invites.cpp
        server/src/workspace_external_sessions.cpp
        server/src/routes/routes_workspace_external_invites.cpp
        server/src/routes/routes_workspace_external_sessions.cpp
        server/src/routes/routes_workspace_external_messages.cpp
        server/src/routes/routes_workspace_external_messages.h
        server/src/workspace_access_shared.cpp
        server/src/runtime_paths.cpp
        server/src/branding_config.cpp
        server/src/backups/system_backup_worker.cpp
        server/src/backups/system_backup_worker.h
        server/src/backups/system_backup_routes.cpp
        server/src/backups/system_backup_routes.h
        server/src/notifications/notification_settings.cpp
        server/src/notifications/notification_settings.h
        server/src/notifications/notification_routes.cpp
        server/src/notifications/notification_routes.h
        server/src/updates/update_center_routes.cpp
        server/src/updates/update_center_routes.h
        server/src/file_versions.cpp
        server/src/file_versions_restore.cpp
        server/src/file_versions_read.cpp
        server/src/archive_zip_manifest.cpp
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
        server/src/routes/routes_circle_nodus_research.cpp
        server/src/routes/routes_circle_nodus_research.h
        server/src/echo_stack_index.cpp
        server/src/echo_stack_text_extract.cpp
        server/src/echo_stack_content_index.cpp
        server/src/echo_stack_search_routes.cpp
        server/src/gallery_albums.cpp
        server/src/gallery_albums_routes.cpp

)

target_link_libraries(pqnas_server PRIVATE
        pqnas_cluster_identity_validation
)

target_link_libraries(pqnas_server PRIVATE
        pqnas_node_enrollment_registry
        pqnas_node_credential_registry
        pqnas_storage_node_credential_runtime
        pqnas_internal_node_credential_rotation_routes
        pqnas_node_deletion_service
        pqnas_node_enrollment_service
        pqnas_node_enrollment_consumption_service
        pqnas_admin_node_enrollment_routes
        pqnas_node_enrollment_bootstrap_routes
        pqnas_storage_node_enrollment_runtime
        pqnas_storage_node_enrollment_https
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
# RT-012: bounded streaming provider for multi-gigabyte version blobs
# -----------------------------------------------------------------------------
add_executable(test_bounded_file_content_provider
        tests/files/test_bounded_file_content_provider.cpp
)

target_include_directories(test_bounded_file_content_provider PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_bounded_file_content_provider
        PRIVATE
        -Werror
    )
endif()

set_target_properties(test_bounded_file_content_provider PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_bounded_file_content_provider
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_bounded_file_content_provider"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_bounded_file_content_provider
)

# -----------------------------------------------------------------------------
# RT-018: workspace/user session cookie cryptographic domain separation
# -----------------------------------------------------------------------------
add_executable(test_session_cookie_domains
        tests/cookies/test_session_cookie_domains.cpp
        server/src/session_cookie.cc
)

target_include_directories(test_session_cookie_domains PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_session_cookie_domains PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_session_cookie_domains
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_session_cookie_domains PRIVATE
        ${SODIUM_LIBRARIES}
)

set_target_properties(test_session_cookie_domains PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_session_cookie_domains
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_session_cookie_domains"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_session_cookie_domains
)

add_custom_target(run_test_workspace_session_domain_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cookies/test_workspace_session_domain_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Tool: pqnas_opaque_helper
#
# Phase 2 scaffold only:
# - no production OPAQUE cryptography
# - no users.json access
# - no session minting
# - future protocol operations fail closed
# -----------------------------------------------------------------------------
add_executable(pqnas_opaque_helper
        tools/opaque_helper/pqnas_opaque_helper.cpp
)

set_target_properties(pqnas_opaque_helper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_pqnas_opaque_helper_self_test
        COMMAND "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper" self-test
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS pqnas_opaque_helper
)


# -----------------------------------------------------------------------------
# Tool: pqnas_opaque_helper_rust
#
# Experimental Rust helper scaffold only:
# - not part of the default ALL build
# - does not replace the current C++ pqnas_opaque_helper yet
# - no production OPAQUE cryptography yet
# - future protocol operations fail closed
# -----------------------------------------------------------------------------
find_program(PQNAS_CARGO cargo)

if (PQNAS_CARGO)
    set(PQNAS_OPAQUE_RUST_DIR "${CMAKE_SOURCE_DIR}/tools/opaque_helper_rust")
    set(PQNAS_OPAQUE_RUST_BIN "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper_rust")

    add_custom_target(pqnas_opaque_helper_rust
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${PQNAS_CARGO} build --manifest-path "${PQNAS_OPAQUE_RUST_DIR}/Cargo.toml"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PQNAS_OPAQUE_RUST_DIR}/target/debug/pqnas_opaque_helper"
            "${PQNAS_OPAQUE_RUST_BIN}"
        BYPRODUCTS "${PQNAS_OPAQUE_RUST_BIN}"
        WORKING_DIRECTORY "${PQNAS_OPAQUE_RUST_DIR}"
        COMMENT "Building experimental Rust OPAQUE helper scaffold"
        VERBATIM
    )

    add_custom_target(run_pqnas_opaque_helper_rust_self_test
        COMMAND "${PQNAS_OPAQUE_RUST_BIN}" self-test
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS pqnas_opaque_helper_rust
    )

    add_custom_target(run_test_opaque_helper_rust
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/opaque_helper_rust/test_opaque_helper_rust.py" "${PQNAS_OPAQUE_RUST_BIN}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS pqnas_opaque_helper_rust
    )
else()
    message(WARNING "cargo not found; pqnas_opaque_helper_rust target will not be available")
endif()


# -----------------------------------------------------------------------------
# Test: test_opaque_helper_client
# -----------------------------------------------------------------------------
add_executable(test_opaque_helper_client
        tests/opaque_helper_client/test_opaque_helper_client.cpp
        server/src/opaque_helper_client.cpp
)

target_include_directories(test_opaque_helper_client PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

set_target_properties(test_opaque_helper_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_opaque_helper_client
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_opaque_helper_client" "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_opaque_helper_client pqnas_opaque_helper
)


# -----------------------------------------------------------------------------
# Integration test: C++ OpaqueHelperClient -> Rust OPAQUE helper registration
# -----------------------------------------------------------------------------
add_executable(test_opaque_helper_client_rust_roundtrip
        tests/opaque_helper_client_rust_roundtrip/test_opaque_helper_client_rust_roundtrip.cpp
        server/src/opaque_helper_client.cpp
)

target_include_directories(test_opaque_helper_client_rust_roundtrip PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

set_target_properties(test_opaque_helper_client_rust_roundtrip PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if (PQNAS_CARGO)
    add_custom_target(run_test_opaque_helper_client_rust_roundtrip
            COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/opaque_helper_client_rust_roundtrip/test_opaque_helper_client_rust_roundtrip.py"
                "${CMAKE_BINARY_DIR}/bin/test_opaque_helper_client_rust_roundtrip"
                "${PQNAS_OPAQUE_RUST_BIN}"
                "${PQNAS_OPAQUE_RUST_DIR}/Cargo.toml"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            DEPENDS test_opaque_helper_client_rust_roundtrip pqnas_opaque_helper_rust
    )
endif()


# -----------------------------------------------------------------------------
# Test: test_opaque_backend_status
# -----------------------------------------------------------------------------
add_executable(test_opaque_backend_status
        tests/opaque_backend_status/test_opaque_backend_status.cpp
        server/src/opaque_backend_status.cpp
        server/src/opaque_helper_client.cpp
        server/src/opaque_credentials.cpp
        server/src/durable_file.cpp
        server/src/runtime_paths.cpp
)

target_include_directories(test_opaque_backend_status PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

set_target_properties(test_opaque_backend_status PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_opaque_backend_status
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_opaque_backend_status"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_opaque_backend_status
)


# -----------------------------------------------------------------------------
# Test: test_opaque_runtime_paths
# -----------------------------------------------------------------------------
add_executable(test_opaque_runtime_paths
        tests/runtime_paths/test_opaque_runtime_paths.cpp
        server/src/runtime_paths.cpp
)

target_include_directories(test_opaque_runtime_paths PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

set_target_properties(test_opaque_runtime_paths PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_opaque_runtime_paths
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_opaque_runtime_paths"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_opaque_runtime_paths
)


target_sources(pqnas_server PRIVATE
        server/src/cluster/user_placement_service.cpp
        server/src/cluster/user_placement_preview_service.cpp
        server/src/cluster/user_placement_recommendation_apply_service.cpp
        server/src/cluster/user_placement_policy_assignment_service.cpp
        server/src/cluster/user_placement_selector.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_policy_registry.cpp
        server/src/routes/routes_admin_cluster_placement_policies.cpp
        server/src/routes/routes_admin_cluster_user_placement.cpp
        server/src/routes/routes_admin_cluster_user_placement_preview.cpp
        server/src/routes/routes_admin_cluster_user_placement_apply.cpp
        server/src/routes/routes_admin_cluster_user_policy.cpp
)

# -----------------------------------------------------------------------------
# Test: test_admin_users_overview_route
# -----------------------------------------------------------------------------
add_executable(test_admin_users_overview_route
        tests/cluster/test_admin_users_overview_route.cpp
        server/src/routes/routes_admin_users_overview.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_admin_users_overview_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_admin_users_overview_route PRIVATE
        Threads::Threads
)

set_target_properties(test_admin_users_overview_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_users_overview_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_users_overview_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_users_overview_route
)


# -----------------------------------------------------------------------------
# Test: test_admin_cluster_placement_policy_routes
# -----------------------------------------------------------------------------
add_executable(test_admin_cluster_placement_policy_routes
        tests/cluster/test_admin_cluster_placement_policy_routes.cpp
        server/src/routes/routes_admin_cluster_placement_policies.cpp
        server/src/cluster/user_placement_policy_registry.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_selector.cpp
)

target_include_directories(test_admin_cluster_placement_policy_routes PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_admin_cluster_placement_policy_routes PRIVATE
        Threads::Threads
)

set_target_properties(test_admin_cluster_placement_policy_routes PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_cluster_placement_policy_routes
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_cluster_placement_policy_routes"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_cluster_placement_policy_routes
)


# -----------------------------------------------------------------------------
# Test: test_admin_cluster_user_placement_route
# -----------------------------------------------------------------------------
add_executable(test_admin_cluster_user_placement_route
        tests/cluster/test_admin_cluster_user_placement_route.cpp
        server/src/routes/routes_admin_cluster_user_placement.cpp
        server/src/cluster/user_placement_service.cpp
        server/src/cluster/node_registry.cpp
        server/src/users_registry.cpp
)

target_link_libraries(test_admin_cluster_user_placement_route PRIVATE
        pqnas_cluster_identity_validation
)

target_include_directories(test_admin_cluster_user_placement_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_admin_cluster_user_placement_route PRIVATE
        Threads::Threads
)

set_target_properties(test_admin_cluster_user_placement_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_cluster_user_placement_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_cluster_user_placement_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_cluster_user_placement_route
)


# -----------------------------------------------------------------------------
# Test: test_user_placement_policy_assignment_service
# -----------------------------------------------------------------------------
add_executable(test_user_placement_policy_assignment_service
        tests/cluster/test_user_placement_policy_assignment_service.cpp
        server/src/cluster/user_placement_policy_assignment_service.cpp
        server/src/cluster/user_placement_policy_registry.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_selector.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_user_placement_policy_assignment_service PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_user_placement_policy_assignment_service PRIVATE
        Threads::Threads
)

set_target_properties(test_user_placement_policy_assignment_service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_placement_policy_assignment_service
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_placement_policy_assignment_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_placement_policy_assignment_service
)


# -----------------------------------------------------------------------------
# Test: test_admin_cluster_user_policy_route
# -----------------------------------------------------------------------------
add_executable(test_admin_cluster_user_policy_route
        tests/cluster/test_admin_cluster_user_policy_route.cpp
        server/src/routes/routes_admin_cluster_user_policy.cpp
        server/src/cluster/user_placement_policy_assignment_service.cpp
        server/src/cluster/user_placement_policy_registry.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_selector.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_admin_cluster_user_policy_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_admin_cluster_user_policy_route PRIVATE
        Threads::Threads
)

set_target_properties(test_admin_cluster_user_policy_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_cluster_user_policy_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_cluster_user_policy_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_cluster_user_policy_route
)


# -----------------------------------------------------------------------------
# Test: test_user_placement_preview_service
# -----------------------------------------------------------------------------
add_executable(test_user_placement_preview_service
        tests/cluster/test_user_placement_preview_service.cpp
        server/src/cluster/user_placement_preview_service.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_selector.cpp
)

target_include_directories(test_user_placement_preview_service PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

target_link_libraries(test_user_placement_preview_service PRIVATE
        Threads::Threads
)

set_target_properties(test_user_placement_preview_service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_placement_preview_service
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_placement_preview_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_placement_preview_service
)


# -----------------------------------------------------------------------------
# Test: test_admin_cluster_user_placement_preview_route
# -----------------------------------------------------------------------------
add_executable(test_admin_cluster_user_placement_preview_route
        tests/cluster/test_admin_cluster_user_placement_preview_route.cpp
        server/src/routes/routes_admin_cluster_user_placement_preview.cpp
        server/src/cluster/user_placement_preview_service.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_selector.cpp
)

target_include_directories(test_admin_cluster_user_placement_preview_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_admin_cluster_user_placement_preview_route PRIVATE
        Threads::Threads
)

set_target_properties(test_admin_cluster_user_placement_preview_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_cluster_user_placement_preview_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_cluster_user_placement_preview_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_cluster_user_placement_preview_route
)


# -----------------------------------------------------------------------------
# Test: test_user_placement_recommendation_apply_service
# -----------------------------------------------------------------------------
add_executable(test_user_placement_recommendation_apply_service
        tests/cluster/test_user_placement_recommendation_apply_service.cpp
        server/src/cluster/user_placement_recommendation_apply_service.cpp
        server/src/cluster/user_placement_preview_service.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_selector.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_user_placement_recommendation_apply_service PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_user_placement_recommendation_apply_service PRIVATE
        Threads::Threads
)

set_target_properties(test_user_placement_recommendation_apply_service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_placement_recommendation_apply_service
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_placement_recommendation_apply_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_placement_recommendation_apply_service
)


# -----------------------------------------------------------------------------
# Test: test_admin_cluster_user_placement_apply_route
# -----------------------------------------------------------------------------
add_executable(test_admin_cluster_user_placement_apply_route
        tests/cluster/test_admin_cluster_user_placement_apply_route.cpp
        server/src/routes/routes_admin_cluster_user_placement_apply.cpp
        server/src/cluster/user_placement_recommendation_apply_service.cpp
        server/src/cluster/user_placement_preview_service.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_selector.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_admin_cluster_user_placement_apply_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_admin_cluster_user_placement_apply_route PRIVATE
        Threads::Threads
)

set_target_properties(test_admin_cluster_user_placement_apply_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_cluster_user_placement_apply_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_cluster_user_placement_apply_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_cluster_user_placement_apply_route
)


# -----------------------------------------------------------------------------
# Test: test_user_placement_selector
# -----------------------------------------------------------------------------
add_executable(test_user_placement_selector
        tests/cluster/test_user_placement_selector.cpp
        server/src/cluster/user_placement_selector.cpp
)

target_include_directories(test_user_placement_selector PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

set_target_properties(test_user_placement_selector PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_placement_selector
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_placement_selector"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_placement_selector
)


# -----------------------------------------------------------------------------
# Test: test_user_placement_policy
# -----------------------------------------------------------------------------
add_executable(test_user_placement_policy
        tests/cluster/test_user_placement_policy.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_selector.cpp
)

target_include_directories(test_user_placement_policy PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

set_target_properties(test_user_placement_policy PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_placement_policy
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_placement_policy"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_placement_policy
)


# -----------------------------------------------------------------------------
# Test: test_user_placement_policy_registry
# -----------------------------------------------------------------------------
add_executable(test_user_placement_policy_registry
        tests/cluster/test_user_placement_policy_registry.cpp
        server/src/cluster/user_placement_policy_registry.cpp
        server/src/cluster/user_placement_policy.cpp
        server/src/cluster/user_placement_selector.cpp
)

target_include_directories(test_user_placement_policy_registry PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (
    CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
    CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
)
    # Reliability regression only: inject one parent-directory fsync failure
    # after the policy registry rename without a production fault hook.
    target_link_options(
        test_user_placement_policy_registry
        PRIVATE
        "-Wl,--wrap=fsync"
    )
endif()

set_target_properties(test_user_placement_policy_registry PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_placement_policy_registry
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_placement_policy_registry"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_placement_policy_registry
)


# -----------------------------------------------------------------------------
# Test: test_user_placement_service
# -----------------------------------------------------------------------------
add_executable(test_user_placement_service
        tests/cluster/test_user_placement_service.cpp
        server/src/cluster/user_placement_service.cpp
        server/src/cluster/node_registry.cpp
        server/src/users_registry.cpp
)

target_link_libraries(test_user_placement_service PRIVATE
        pqnas_cluster_identity_validation
)

target_include_directories(test_user_placement_service PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_user_placement_service PRIVATE
        Threads::Threads
)

set_target_properties(test_user_placement_service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_placement_service
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_placement_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_placement_service
)


# -----------------------------------------------------------------------------
# Test: test_user_home_node
# -----------------------------------------------------------------------------
add_executable(test_user_home_node
        tests/cluster/test_user_home_node.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_user_home_node PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_user_home_node PRIVATE
        Threads::Threads
)

if (
    CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
    CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
)
    # Security regression only: inject parent-directory fsync failure after
    # rename without adding any production runtime fault hook.
    target_link_options(
        test_user_home_node
        PRIVATE
        "-Wl,--wrap=fsync"
    )
endif()

set_target_properties(test_user_home_node PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_home_node
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_home_node"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_home_node
)


# -----------------------------------------------------------------------------
# Guard: admin profile updates must remain profile-only (RT-005)
# -----------------------------------------------------------------------------
add_custom_target(run_test_admin_user_profile_stale_write_guard
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_admin_user_profile_stale_write_guard.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Guard: production wiring checks include main.cpp local .inc files (RT-011)
# -----------------------------------------------------------------------------
add_custom_target(run_test_production_translation_unit
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_production_translation_unit.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Test: test_node_heartbeat_worker
# -----------------------------------------------------------------------------
add_executable(test_node_heartbeat_worker
        tests/cluster/test_node_heartbeat_worker.cpp
        server/src/cluster/node_heartbeat_worker.cpp
        server/src/cluster/node_registry.cpp
        server/src/cluster/node_status.cpp
        server/src/cluster/internal_auth.cpp
        server/src/storage_info.cc
)

target_link_libraries(test_node_heartbeat_worker PRIVATE
        pqnas_cluster_identity_validation
        pqnas_node_credential_registry
)

target_include_directories(test_node_heartbeat_worker PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_node_heartbeat_worker PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(test_node_heartbeat_worker PRIVATE
        pqnas_verify_v4
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_node_heartbeat_worker PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_node_heartbeat_worker
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_node_heartbeat_worker"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_heartbeat_worker
)


# -----------------------------------------------------------------------------
# Test: Director Node credential rotation service
# -----------------------------------------------------------------------------
add_executable(test_node_credential_rotation_service
        tests/cluster/test_node_credential_rotation_service.cpp
        server/src/cluster/internal_auth.cpp
        server/src/cluster/node_registry.cpp
)

target_include_directories(
        test_node_credential_rotation_service
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        test_node_credential_rotation_service
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_node_credential_rotation_service
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        test_node_credential_rotation_service
        PRIVATE
        pqnas_node_credential_rotation_service
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(
        test_node_credential_rotation_service
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_node_credential_rotation_service
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_node_credential_rotation_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_credential_rotation_service
)


# -----------------------------------------------------------------------------
# Test: test_node_deletion_service
# -----------------------------------------------------------------------------
add_executable(test_node_deletion_service
        tests/cluster/test_node_deletion_service.cpp
        server/src/cluster/internal_auth.cpp
        server/src/cluster/node_registry.cpp
)

target_include_directories(test_node_deletion_service PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_node_deletion_service PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(test_node_deletion_service PRIVATE
        pqnas_node_deletion_service
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_node_deletion_service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_node_deletion_service
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_node_deletion_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_deletion_service
)


# -----------------------------------------------------------------------------
# Test: test_node_enrollment_consumption_service
# -----------------------------------------------------------------------------
add_executable(test_node_enrollment_consumption_service
        tests/cluster/test_node_enrollment_consumption_service.cpp
        server/src/cluster/internal_auth.cpp
        server/src/cluster/node_registry.cpp
)

target_include_directories(
        test_node_enrollment_consumption_service
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        test_node_enrollment_consumption_service
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(
        test_node_enrollment_consumption_service
        PRIVATE
        pqnas_node_enrollment_consumption_service
        ${SODIUM_LIBRARIES}
)

set_target_properties(
        test_node_enrollment_consumption_service
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_node_enrollment_consumption_service
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_node_enrollment_consumption_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_enrollment_consumption_service
)


# -----------------------------------------------------------------------------
# Test: test_node_enrollment_bootstrap_route
# -----------------------------------------------------------------------------
add_executable(test_node_enrollment_bootstrap_route
        tests/cluster/test_node_enrollment_bootstrap_route.cpp
        server/src/cluster/internal_auth.cpp
        server/src/cluster/node_registry.cpp
)

target_include_directories(
        test_node_enrollment_bootstrap_route
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        test_node_enrollment_bootstrap_route
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(
        test_node_enrollment_bootstrap_route
        PRIVATE
        pqnas_node_enrollment_bootstrap_routes
        ${SODIUM_LIBRARIES}
)

set_target_properties(
        test_node_enrollment_bootstrap_route
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_node_enrollment_bootstrap_route
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_node_enrollment_bootstrap_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_enrollment_bootstrap_route
)


# -----------------------------------------------------------------------------
# Test: test_admin_node_enrollment_routes
# -----------------------------------------------------------------------------
add_executable(test_admin_node_enrollment_routes
        tests/cluster/test_admin_node_enrollment_routes.cpp
)

target_link_libraries(test_admin_node_enrollment_routes PRIVATE
        pqnas_admin_node_enrollment_routes
)

target_include_directories(test_admin_node_enrollment_routes PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

set_target_properties(test_admin_node_enrollment_routes PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_node_enrollment_routes
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_node_enrollment_routes"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_node_enrollment_routes
)


# -----------------------------------------------------------------------------
# Test: test_node_enrollment_revocation_service
# -----------------------------------------------------------------------------
add_executable(test_node_enrollment_revocation_service
        tests/cluster/test_node_enrollment_revocation_service.cpp
)

target_link_libraries(test_node_enrollment_revocation_service PRIVATE
        pqnas_node_enrollment_service
)

set_target_properties(test_node_enrollment_revocation_service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_node_enrollment_revocation_service
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_node_enrollment_revocation_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_enrollment_revocation_service
)


# -----------------------------------------------------------------------------
# Test: test_node_enrollment_service
# -----------------------------------------------------------------------------
add_executable(test_node_enrollment_service
        tests/cluster/test_node_enrollment_service.cpp
)

target_link_libraries(test_node_enrollment_service PRIVATE
        pqnas_node_enrollment_service
)

set_target_properties(test_node_enrollment_service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_node_enrollment_service
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_node_enrollment_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_enrollment_service
)


# -----------------------------------------------------------------------------
# Test: test_node_enrollment_token
# -----------------------------------------------------------------------------
add_executable(test_node_enrollment_token
        tests/cluster/test_node_enrollment_token.cpp
)

target_link_libraries(test_node_enrollment_token PRIVATE
        pqnas_node_enrollment_token
)

set_target_properties(test_node_enrollment_token PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_node_enrollment_token
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_node_enrollment_token"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_enrollment_token
)


# -----------------------------------------------------------------------------
# Test: test_storage_handoff_ticket_store
# -----------------------------------------------------------------------------
add_executable(test_storage_handoff_ticket_store
        tests/cluster/test_storage_handoff_ticket_store.cpp
)

target_include_directories(test_storage_handoff_ticket_store PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_handoff_ticket_store PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(test_storage_handoff_ticket_store PRIVATE
        pqnas_storage_handoff_ticket_store
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_storage_handoff_ticket_store PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_handoff_ticket_store
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_handoff_ticket_store"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_handoff_ticket_store
)


# -----------------------------------------------------------------------------
# Test: test_internal_storage_handoff_route
# -----------------------------------------------------------------------------
add_executable(test_internal_storage_handoff_route
        tests/cluster/test_internal_storage_handoff_route.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_internal_storage_handoff_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_internal_storage_handoff_route PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(test_internal_storage_handoff_route PRIVATE
        pqnas_internal_storage_handoff_routes
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_internal_storage_handoff_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_internal_storage_handoff_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_internal_storage_handoff_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_internal_storage_handoff_route
)


# -----------------------------------------------------------------------------
# Test: test_node_enrollment_registry
# -----------------------------------------------------------------------------
add_executable(test_node_enrollment_registry
        tests/cluster/test_node_enrollment_registry.cpp
)

target_link_libraries(test_node_enrollment_registry PRIVATE
        pqnas_node_enrollment_registry
)

set_target_properties(test_node_enrollment_registry PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_node_enrollment_registry
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_node_enrollment_registry"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_enrollment_registry
)


# -----------------------------------------------------------------------------
# Test: test_node_credential_registry
# -----------------------------------------------------------------------------
add_executable(test_node_credential_registry
        tests/cluster/test_node_credential_registry.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_node_credential_registry PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_node_credential_registry PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(test_node_credential_registry PRIVATE
        pqnas_node_credential_registry
        ${SODIUM_LIBRARIES}
)

if (
    CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
    CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
)
    # Security regression only: inject one parent-directory fsync failure
    # after rename without adding a production runtime fault hook.
    target_link_options(
        test_node_credential_registry
        PRIVATE
        "-Wl,--wrap=fsync"
    )
endif()

set_target_properties(test_node_credential_registry PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_node_credential_registry
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_node_credential_registry"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_credential_registry
)


# -----------------------------------------------------------------------------
# Test: test_storage_node_enrollment_runtime
# -----------------------------------------------------------------------------
add_executable(test_storage_node_enrollment_runtime
        tests/cluster/test_storage_node_enrollment_runtime.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(
        test_storage_node_enrollment_runtime
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        test_storage_node_enrollment_runtime
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(
        test_storage_node_enrollment_runtime
        PRIVATE
        pqnas_storage_node_enrollment_runtime
        ${SODIUM_LIBRARIES}
)

set_target_properties(
        test_storage_node_enrollment_runtime
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_storage_node_enrollment_runtime
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_storage_node_enrollment_runtime"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_node_enrollment_runtime
)


# -----------------------------------------------------------------------------
# Test: test_storage_node_enrollment_client
# -----------------------------------------------------------------------------
add_executable(test_storage_node_enrollment_client
        tests/cluster/test_storage_node_enrollment_client.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(
        test_storage_node_enrollment_client
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        test_storage_node_enrollment_client
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(
        test_storage_node_enrollment_client
        PRIVATE
        pqnas_storage_node_enrollment_client
        ${SODIUM_LIBRARIES}
)

set_target_properties(
        test_storage_node_enrollment_client
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_storage_node_enrollment_client
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_storage_node_enrollment_client"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_node_enrollment_client
)


# -----------------------------------------------------------------------------
# Test: test_storage_node_enrollment_https
# -----------------------------------------------------------------------------
add_executable(test_storage_node_enrollment_https
        tests/cluster/test_storage_node_enrollment_https.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(
        test_storage_node_enrollment_https
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_definitions(
        test_storage_node_enrollment_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

target_compile_options(
        test_storage_node_enrollment_https
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(
        test_storage_node_enrollment_https
        PRIVATE
        pqnas_storage_node_enrollment_https
        ${SODIUM_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
)

set_target_properties(
        test_storage_node_enrollment_https
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_storage_node_enrollment_https
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_storage_node_enrollment_https"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_node_enrollment_https
)


# -----------------------------------------------------------------------------
# Test: test_storage_node_provisioning_store
# -----------------------------------------------------------------------------
add_executable(test_storage_node_provisioning_store
        tests/cluster/test_storage_node_provisioning_store.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(
        test_storage_node_provisioning_store
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        test_storage_node_provisioning_store
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(
        test_storage_node_provisioning_store
        PRIVATE
        pqnas_storage_node_provisioning_store
        ${SODIUM_LIBRARIES}
)

set_target_properties(
        test_storage_node_provisioning_store
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_storage_node_provisioning_store
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_storage_node_provisioning_store"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_node_provisioning_store
)


# -----------------------------------------------------------------------------
# Test: test_cluster_identity_validation
# -----------------------------------------------------------------------------
add_executable(test_cluster_identity_validation
        tests/cluster/test_cluster_identity_validation.cpp
)

target_link_libraries(test_cluster_identity_validation PRIVATE
        pqnas_cluster_identity_validation
)

set_target_properties(test_cluster_identity_validation PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_cluster_identity_validation
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_cluster_identity_validation"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_cluster_identity_validation
)


# -----------------------------------------------------------------------------
# Test: test_admin_cluster_node_routes
# -----------------------------------------------------------------------------
add_executable(test_admin_cluster_node_routes
        tests/cluster/test_admin_cluster_node_routes.cpp
        server/src/routes/routes_admin_cluster_nodes.cpp
        server/src/cluster/node_registry.cpp
)

target_link_libraries(test_admin_cluster_node_routes PRIVATE
        pqnas_cluster_identity_validation
)

target_include_directories(test_admin_cluster_node_routes PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

# RT014: admin Node TLS migration validates canonical SPKI pins.
target_link_libraries(test_admin_cluster_node_routes PRIVATE
        pqnas_node_tls_identity
)

set_target_properties(test_admin_cluster_node_routes PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_cluster_node_routes
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_cluster_node_routes"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_cluster_node_routes
)


# -----------------------------------------------------------------------------
# Test: test_core_ui_shell_routes
# -----------------------------------------------------------------------------
add_executable(test_core_ui_shell_routes
        tests/cluster/test_core_ui_shell_routes.cpp
        server/src/routes/routes_core_ui_shell.cpp
)

target_include_directories(test_core_ui_shell_routes PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

set_target_properties(test_core_ui_shell_routes PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_core_ui_shell_routes
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_core_ui_shell_routes"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_core_ui_shell_routes
)


# -----------------------------------------------------------------------------
# Test: test_admin_api_catalog_cluster_routes
# -----------------------------------------------------------------------------
add_custom_target(run_test_admin_api_catalog_cluster_routes
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_admin_api_catalog_cluster_routes.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Test: test_node_registry
# -----------------------------------------------------------------------------
add_executable(test_node_registry
        tests/cluster/test_node_registry.cpp
        server/src/cluster/node_registry.cpp
)

target_link_libraries(test_node_registry PRIVATE
        pqnas_cluster_identity_validation
)

target_include_directories(test_node_registry PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

set_target_properties(test_node_registry PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_node_registry
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_node_registry"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_registry
)


# -----------------------------------------------------------------------------
# Test: test_internal_node_credential_rotation_route
# -----------------------------------------------------------------------------
add_executable(test_internal_node_credential_rotation_route
        tests/cluster/test_internal_node_credential_rotation_route.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(
        test_internal_node_credential_rotation_route
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        test_internal_node_credential_rotation_route
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_internal_node_credential_rotation_route
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        test_internal_node_credential_rotation_route
        PRIVATE
        pqnas_internal_node_credential_rotation_routes
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(
        test_internal_node_credential_rotation_route
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_internal_node_credential_rotation_route
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_internal_node_credential_rotation_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_internal_node_credential_rotation_route
)


# -----------------------------------------------------------------------------
# Test: test_internal_node_status_route
# -----------------------------------------------------------------------------
add_executable(test_internal_node_status_route
        tests/cluster/test_internal_node_status_route.cpp
        server/src/routes/routes_internal_node_status.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_internal_node_status_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_internal_node_status_route PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(test_internal_node_status_route PRIVATE
        ${SODIUM_LIBRARIES}
)

set_target_properties(test_internal_node_status_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_internal_node_status_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_internal_node_status_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_internal_node_status_route
)


# -----------------------------------------------------------------------------
# Test: test_node_status
# -----------------------------------------------------------------------------
add_executable(test_node_status
        tests/cluster/test_node_status.cpp
        server/src/cluster/node_status.cpp
        server/src/storage_info.cc
)

target_link_libraries(test_node_status PRIVATE
        pqnas_cluster_identity_validation
)

target_include_directories(test_node_status PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_node_status PRIVATE
        pqnas_verify_v4
)

set_target_properties(test_node_status PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_node_status
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_node_status"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_status
)


# -----------------------------------------------------------------------------
# Test: test_internal_auth
# -----------------------------------------------------------------------------
add_executable(test_internal_auth
        tests/cluster/test_internal_auth.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_internal_auth PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_internal_auth PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(test_internal_auth PRIVATE
        ${SODIUM_LIBRARIES}
)

set_target_properties(test_internal_auth PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_internal_auth
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_internal_auth"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_internal_auth
)


# -----------------------------------------------------------------------------
# Test: test_storage_node_credential_runtime
# -----------------------------------------------------------------------------
add_executable(test_storage_node_credential_runtime
        tests/cluster/test_storage_node_credential_runtime.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(
        test_storage_node_credential_runtime
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(
        test_storage_node_credential_runtime
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_node_credential_runtime
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        test_storage_node_credential_runtime
        PRIVATE
        pqnas_storage_node_credential_runtime
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(
        test_storage_node_credential_runtime
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_storage_node_credential_runtime
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_storage_node_credential_runtime"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_node_credential_runtime
)


# -----------------------------------------------------------------------------
# Test: test_server_role_config
# -----------------------------------------------------------------------------
add_executable(test_server_role_config
        tests/cluster/test_server_role_config.cpp
        server/src/cluster/server_role.cpp
)

target_link_libraries(test_server_role_config PRIVATE
        pqnas_cluster_identity_validation
)

target_include_directories(test_server_role_config PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

set_target_properties(test_server_role_config PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_server_role_config
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_server_role_config"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_server_role_config
)


# -----------------------------------------------------------------------------
# Test: test_opaque_credentials
# -----------------------------------------------------------------------------
add_executable(test_opaque_credentials
        tests/opaque_credentials/test_opaque_credentials.cpp
        server/src/opaque_credentials.cpp
        server/src/durable_file.cpp
)

target_include_directories(test_opaque_credentials PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

set_target_properties(test_opaque_credentials PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_opaque_credentials
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_opaque_credentials"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_opaque_credentials
)


# -----------------------------------------------------------------------------
# Runtime test: OPAQUE admin enrollment endpoint
# -----------------------------------------------------------------------------
add_custom_target(run_test_opaque_admin_enrollment_runtime
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/opaque_admin_enrollment/test_opaque_admin_enrollment_runtime.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

if (PQNAS_CARGO)
    add_custom_target(run_test_opaque_admin_registration_positive_runtime
        COMMAND python3
            "${CMAKE_SOURCE_DIR}/tests/opaque_admin_registration/test_opaque_admin_registration_positive_runtime.py"
            "${PQNAS_OPAQUE_RUST_DIR}/Cargo.toml"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS pqnas_opaque_helper_rust
    )
endif()


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



# -----------------------------------------------------------------------------
# Test: test_interactive_user_login_policy
# -----------------------------------------------------------------------------
add_executable(test_interactive_user_login_policy
        tests/auth/test_interactive_user_login_policy.cpp
        server/src/interactive_user_login_policy.cc
)

target_include_directories(test_interactive_user_login_policy PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_interactive_user_login_policy PRIVATE
        Threads::Threads
)

set_target_properties(test_interactive_user_login_policy PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_interactive_user_login_policy
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_interactive_user_login_policy"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_interactive_user_login_policy
)

# -----------------------------------------------------------------------------
# Test: Node interactive-auth structural guards
# -----------------------------------------------------------------------------
add_custom_target(run_test_node_interactive_auth_guards
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/auth/test_node_interactive_auth_guards.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Test: RT-001 legacy Node route/auth structural guards
# -----------------------------------------------------------------------------
add_custom_target(run_test_legacy_file_routes_role_gating
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_legacy_file_routes_role_gating.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)



# -----------------------------------------------------------------------------
# Test: storage metadata validation
# -----------------------------------------------------------------------------
add_executable(test_storage_metadata_validation
        tests/cluster/test_storage_metadata_validation.cpp
)

target_include_directories(test_storage_metadata_validation PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_metadata_validation
        PRIVATE
        -Werror
    )
endif()

set_target_properties(test_storage_metadata_validation PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_metadata_validation
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_metadata_validation"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_metadata_validation
)

# -----------------------------------------------------------------------------
# Test: storage path validation
# -----------------------------------------------------------------------------
add_executable(test_storage_path_validation
        tests/cluster/test_storage_path_validation.cpp
)

target_include_directories(test_storage_path_validation PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

target_link_libraries(test_storage_path_validation PRIVATE
        pqnas_storage_path_validation
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_path_validation
        PRIVATE
        -Werror
    )
endif()

set_target_properties(test_storage_path_validation PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_path_validation
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_path_validation"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_path_validation
)

# -----------------------------------------------------------------------------
# Shared storage path validation
# -----------------------------------------------------------------------------
add_library(pqnas_storage_path_validation STATIC
        server/src/storage_path_validation.cpp
        server/src/storage_path_validation.h
)

target_include_directories(pqnas_storage_path_validation PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_path_validation
        PRIVATE
        -Werror
    )
endif()


# -----------------------------------------------------------------------------
# Test: canonical existing-path containment
# -----------------------------------------------------------------------------
add_executable(test_storage_canonical_path_containment
        tests/cluster/test_storage_canonical_path_containment.cpp
)

target_include_directories(test_storage_canonical_path_containment PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_canonical_path_containment
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_canonical_path_containment PRIVATE
        pqnas_storage_path_validation
)

set_target_properties(test_storage_canonical_path_containment PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_canonical_path_containment
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_canonical_path_containment"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_canonical_path_containment
)

# -----------------------------------------------------------------------------
# Persistent Storage user projection store
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_store STATIC
        server/src/cluster/storage_user_projection_store.cpp
        server/src/cluster/storage_user_projection_store.h
)

target_include_directories(pqnas_storage_user_projection_store PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_store
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_store PUBLIC
        pqnas_cluster_identity_validation
        pqnas_storage_path_validation
)

# -----------------------------------------------------------------------------
# Test: Storage user projection store
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_store
        tests/cluster/test_storage_user_projection_store.cpp
)

target_include_directories(test_storage_user_projection_store PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_store
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_store PRIVATE
        pqnas_storage_user_projection_store
)

set_target_properties(test_storage_user_projection_store PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_store
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_store"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_store
)

# -----------------------------------------------------------------------------
# Storage user projection audit primitives (RT-008)
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_audit STATIC
        server/src/cluster/storage_user_projection_audit.cpp
        server/src/cluster/storage_user_projection_audit.h
)

target_include_directories(pqnas_storage_user_projection_audit PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_audit
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_audit PUBLIC
        pqnas_cluster_identity_validation
        pqnas_storage_path_validation
        OpenSSL::Crypto
)

# -----------------------------------------------------------------------------
# Test: Storage user projection audit primitives
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_audit
        tests/cluster/test_storage_user_projection_audit.cpp
)

target_include_directories(test_storage_user_projection_audit PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_audit
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_audit PRIVATE
        pqnas_storage_user_projection_audit
)

set_target_properties(test_storage_user_projection_audit PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_audit
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_audit"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_audit
)

# -----------------------------------------------------------------------------
# Storage pool runtime registry
# -----------------------------------------------------------------------------
add_library(pqnas_storage_pool_runtime STATIC
        server/src/storage_pool_runtime.cpp
        server/src/storage_pool_runtime.h
)

target_include_directories(pqnas_storage_pool_runtime PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_pool_runtime
        PRIVATE
        -Werror
    )
endif()

# -----------------------------------------------------------------------------
# Test: Storage pool runtime registry
# -----------------------------------------------------------------------------
add_executable(test_storage_pool_runtime
        tests/test_storage_pool_runtime.cpp
)

target_include_directories(test_storage_pool_runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_pool_runtime
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_pool_runtime PRIVATE
        pqnas_storage_pool_runtime
)

set_target_properties(test_storage_pool_runtime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_pool_runtime
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_pool_runtime"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_pool_runtime
)

# Production server uses the shared Node-local runtime pool registry.
target_link_libraries(pqnas_server PRIVATE
        pqnas_storage_pool_runtime
)

# -----------------------------------------------------------------------------
# Storage Node user root resolver
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_root_resolver STATIC
        server/src/cluster/storage_user_root_resolver.cpp
        server/src/cluster/storage_user_root_resolver.h
)

target_include_directories(pqnas_storage_user_root_resolver PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_root_resolver
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_root_resolver PUBLIC
        pqnas_storage_user_projection_store
)

# -----------------------------------------------------------------------------
# Test: Storage Node user root resolver
# -----------------------------------------------------------------------------
add_executable(test_storage_user_root_resolver
        tests/cluster/test_storage_user_root_resolver.cpp
)

target_include_directories(test_storage_user_root_resolver PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_root_resolver
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_root_resolver PRIVATE
        pqnas_storage_user_root_resolver
)

set_target_properties(test_storage_user_root_resolver PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_root_resolver
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_root_resolver"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_root_resolver
)

# -----------------------------------------------------------------------------
# Storage Node user session authorization policy
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_session_policy STATIC
        server/src/cluster/storage_user_session_policy.cpp
        server/src/cluster/storage_user_session_policy.h
)

target_include_directories(pqnas_storage_user_session_policy PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_session_policy
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_session_policy PUBLIC
        pqnas_cluster_identity_validation
)

# -----------------------------------------------------------------------------
# Test: Storage Node user session authorization policy
# -----------------------------------------------------------------------------
add_executable(test_storage_user_session_policy
        tests/cluster/test_storage_user_session_policy.cpp
)

target_include_directories(test_storage_user_session_policy PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_session_policy
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_session_policy PRIVATE
        pqnas_storage_user_session_policy
)

set_target_properties(test_storage_user_session_policy PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_session_policy
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_session_policy"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_session_policy
)

# -----------------------------------------------------------------------------
# Domain-separated Storage Node session cookie primitive
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_session_cookie STATIC
        server/src/cluster/storage_user_session_cookie.cpp
        server/src/cluster/storage_user_session_cookie.h
)

target_include_directories(pqnas_storage_user_session_cookie PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_storage_user_session_cookie PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_session_cookie
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_session_cookie PUBLIC
        pqnas_cluster_identity_validation
        ${SODIUM_LIBRARIES}
)

# -----------------------------------------------------------------------------
# Test: domain-separated Storage Node session cookie
# -----------------------------------------------------------------------------
add_executable(test_storage_user_session_cookie
        tests/cluster/test_storage_user_session_cookie.cpp
        server/src/session_cookie.cc
)

target_include_directories(test_storage_user_session_cookie PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_user_session_cookie PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_session_cookie
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_session_cookie PRIVATE
        pqnas_storage_user_session_cookie
        OpenSSL::Crypto
        ${SODIUM_LIBRARIES}
)

set_target_properties(test_storage_user_session_cookie PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_session_cookie
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_session_cookie"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_session_cookie
)

# -----------------------------------------------------------------------------
# Storage browser handoff consume service
# -----------------------------------------------------------------------------
add_library(pqnas_storage_handoff_consume_service STATIC
        server/src/cluster/storage_handoff_consume_service.cpp
        server/src/cluster/storage_handoff_consume_service.h
)

target_include_directories(pqnas_storage_handoff_consume_service PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_handoff_consume_service
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_handoff_consume_service PUBLIC
        pqnas_storage_handoff_ticket_store
        pqnas_storage_user_session_policy
        pqnas_storage_user_session_cookie
)

# -----------------------------------------------------------------------------
# Storage browser handoff preauth route
# -----------------------------------------------------------------------------
add_library(pqnas_storage_handoff_preauth_routes STATIC
        server/src/routes/routes_storage_handoff_preauth.cpp
        server/src/routes/routes_storage_handoff_preauth.h
)

target_include_directories(pqnas_storage_handoff_preauth_routes PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(pqnas_storage_handoff_preauth_routes PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_handoff_preauth_routes
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_handoff_preauth_routes PUBLIC
        pqnas_cluster_identity_validation
        pqnas_storage_handoff_ticket_store
)


# -----------------------------------------------------------------------------
# Storage browser handoff consume route
# -----------------------------------------------------------------------------
add_library(pqnas_storage_handoff_consume_routes STATIC
        server/src/routes/routes_storage_handoff_consume.cpp
        server/src/routes/routes_storage_handoff_consume.h
)

target_include_directories(pqnas_storage_handoff_consume_routes PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(pqnas_storage_handoff_consume_routes PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_handoff_consume_routes
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_handoff_consume_routes PUBLIC
        pqnas_storage_handoff_consume_service
        pqnas_cluster_identity_validation
        pqnas_storage_handoff_ticket_store
        pqnas_storage_user_session_cookie
)


# -----------------------------------------------------------------------------
# Test: Storage browser handoff consume service
# -----------------------------------------------------------------------------
add_executable(test_storage_handoff_consume_service
        tests/cluster/test_storage_handoff_consume_service.cpp
        server/src/session_cookie.cc
)

target_include_directories(test_storage_handoff_consume_service PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_handoff_consume_service PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_handoff_consume_service
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_handoff_consume_service PRIVATE
        pqnas_storage_handoff_consume_service
        OpenSSL::Crypto
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_storage_handoff_consume_service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_handoff_consume_service
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_handoff_consume_service"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_handoff_consume_service
)

# -----------------------------------------------------------------------------
# Test: Storage browser handoff consume route
# -----------------------------------------------------------------------------
add_executable(test_storage_handoff_consume_route
        tests/cluster/test_storage_handoff_consume_route.cpp
        server/src/session_cookie.cc
)

target_include_directories(test_storage_handoff_consume_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_handoff_consume_route
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_handoff_consume_route PRIVATE
        pqnas_storage_handoff_consume_routes
        OpenSSL::Crypto
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_storage_handoff_consume_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_handoff_consume_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_handoff_consume_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_handoff_consume_route
)

# Security: browser handoff remains closed until the complete role-aware
# production trust chain and browser E2E flow are ready.
add_custom_target(run_test_storage_handoff_consume_production_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_handoff_consume_production_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Test: Storage browser handoff preauth route
# -----------------------------------------------------------------------------
add_executable(test_storage_handoff_preauth_route
        tests/cluster/test_storage_handoff_preauth_route.cpp
)

target_include_directories(test_storage_handoff_preauth_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_handoff_preauth_route PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_handoff_preauth_route
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_handoff_preauth_route PRIVATE
        pqnas_storage_handoff_preauth_routes
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_storage_handoff_preauth_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_handoff_preauth_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_handoff_preauth_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_handoff_preauth_route
)

# Security: keep the browser-facing handoff chain closed until its complete
# production trust path and E2E tests are ready.
add_custom_target(run_test_storage_handoff_preauth_production_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_handoff_preauth_production_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# Security: the service-to-service ticket endpoint may be production-enabled
# independently while both browser-facing handoff endpoints remain closed.
add_custom_target(run_test_internal_storage_handoff_production_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_internal_storage_handoff_production_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Storage Node session authenticator
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_session_authenticator STATIC
        server/src/cluster/storage_user_session_authenticator.cpp
        server/src/cluster/storage_user_session_authenticator.h
)

target_include_directories(pqnas_storage_user_session_authenticator PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(pqnas_storage_user_session_authenticator PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_session_authenticator
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_session_authenticator PUBLIC
        pqnas_storage_user_session_policy
        pqnas_storage_user_session_cookie
        ${SODIUM_LIBRARIES}
)

# -----------------------------------------------------------------------------
# Storage Node request authentication boundary
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_request_auth STATIC
        server/src/cluster/storage_user_request_auth.cpp
        server/src/cluster/storage_user_request_auth.h
)

target_include_directories(pqnas_storage_user_request_auth PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(pqnas_storage_user_request_auth PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_request_auth
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_request_auth PUBLIC
        pqnas_storage_user_session_authenticator
)


# -----------------------------------------------------------------------------
# Test: Storage Node session authenticator
# -----------------------------------------------------------------------------
add_executable(test_storage_user_session_authenticator
        tests/cluster/test_storage_user_session_authenticator.cpp
        server/src/session_cookie.cc
)

target_include_directories(test_storage_user_session_authenticator PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_user_session_authenticator PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_session_authenticator
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_session_authenticator PRIVATE
        pqnas_storage_user_session_authenticator
        OpenSSL::Crypto
        ${SODIUM_LIBRARIES}
)

set_target_properties(test_storage_user_session_authenticator PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_session_authenticator
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_session_authenticator"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_session_authenticator
)

# -----------------------------------------------------------------------------
# Test: Storage Node request authentication boundary
# -----------------------------------------------------------------------------
add_executable(test_storage_user_request_auth
        tests/cluster/test_storage_user_request_auth.cpp
        server/src/session_cookie.cc
)

target_include_directories(test_storage_user_request_auth PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_user_request_auth PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_request_auth
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_request_auth PRIVATE
        pqnas_storage_user_request_auth
        OpenSSL::Crypto
        ${SODIUM_LIBRARIES}
)

set_target_properties(test_storage_user_request_auth PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_request_auth
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_request_auth"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_request_auth
)


# -----------------------------------------------------------------------------
# Storage Node user access runtime composition
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_access_runtime STATIC
        server/src/cluster/storage_user_access_runtime.cpp
        server/src/cluster/storage_user_access_runtime.h
)

target_include_directories(pqnas_storage_user_access_runtime PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_access_runtime
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_access_runtime PUBLIC
        pqnas_storage_user_request_auth
        pqnas_storage_user_root_resolver
)

# -----------------------------------------------------------------------------
# Test: Storage Node user access runtime composition
# -----------------------------------------------------------------------------
add_executable(test_storage_user_access_runtime
        tests/cluster/test_storage_user_access_runtime.cpp
        server/src/session_cookie.cc
)

target_include_directories(test_storage_user_access_runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_user_access_runtime PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_access_runtime
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_access_runtime PRIVATE
        pqnas_storage_user_access_runtime
        OpenSSL::Crypto
        ${SODIUM_LIBRARIES}
)

set_target_properties(test_storage_user_access_runtime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_access_runtime
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_access_runtime"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_access_runtime
)

# Security: composition may exist before browser handoff or user file routes
# are production-enabled. Keep that trust boundary structurally closed.
add_custom_target(run_test_storage_user_access_runtime_production_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_user_access_runtime_production_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Security: the first Storage user file routes must use the Node-only
# authentication/root chain while browser handoff remains closed.
add_custom_target(run_test_storage_archive_blob_production_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_archive_blob_production_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Internal Storage user projection routes
# -----------------------------------------------------------------------------
add_library(pqnas_internal_storage_user_projection_routes STATIC
        server/src/routes/routes_internal_storage_user_projection.cpp
        server/src/routes/routes_internal_storage_user_projection.h
)

target_include_directories(pqnas_internal_storage_user_projection_routes PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(pqnas_internal_storage_user_projection_routes PRIVATE
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_internal_storage_user_projection_routes
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_internal_storage_user_projection_routes PUBLIC
        pqnas_storage_user_projection_store
        pqnas_storage_user_projection_audit
)


# Node production composition.
#
# storage_resolver uses the shared path validator directly, while the internal
# projection route brings the projection store into the server.
target_link_libraries(pqnas_server PRIVATE
        pqnas_storage_path_validation
        pqnas_internal_storage_user_projection_routes
        pqnas_internal_storage_handoff_routes
        pqnas_storage_user_access_runtime
)

# -----------------------------------------------------------------------------
# Test: internal Storage user projection route
# -----------------------------------------------------------------------------
add_executable(test_internal_storage_user_projection_route
        tests/cluster/test_internal_storage_user_projection_route.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_internal_storage_user_projection_route PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_internal_storage_user_projection_route PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_internal_storage_user_projection_route
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_internal_storage_user_projection_route PRIVATE
        pqnas_internal_storage_user_projection_routes
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_internal_storage_user_projection_route PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_internal_storage_user_projection_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_internal_storage_user_projection_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_internal_storage_user_projection_route
)

# -----------------------------------------------------------------------------
# Structural guard: Node credential rotation production wiring
# -----------------------------------------------------------------------------
add_custom_target(run_test_node_credential_rotation_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_node_credential_rotation_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Structural guard: Storage user projection production wiring
# -----------------------------------------------------------------------------
add_custom_target(run_test_storage_user_projection_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_user_projection_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Test: Director-owned user Storage projection revisions
# -----------------------------------------------------------------------------
add_executable(test_user_storage_projection_revision
        tests/cluster/test_user_storage_projection_revision.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_user_storage_projection_revision PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_user_storage_projection_revision
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_user_storage_projection_revision PRIVATE
        Threads::Threads
)

set_target_properties(test_user_storage_projection_revision PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_storage_projection_revision
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_storage_projection_revision"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_storage_projection_revision
)

# -----------------------------------------------------------------------------
# Test: post-commit Storage projection reconciliation notifier
# -----------------------------------------------------------------------------
add_executable(test_storage_projection_commit_notifier
        tests/cluster/test_storage_projection_commit_notifier.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_storage_projection_commit_notifier PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_projection_commit_notifier
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_projection_commit_notifier PRIVATE
        Threads::Threads
)

set_target_properties(test_storage_projection_commit_notifier PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_projection_commit_notifier
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_projection_commit_notifier"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_projection_commit_notifier
)

# -----------------------------------------------------------------------------
# Structural guard: projection-relevant status callers use write-through API
# -----------------------------------------------------------------------------
add_custom_target(run_test_projection_status_persisted_callers
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/auth/test_projection_status_persisted_callers.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Test: atomic persisted user storage metadata + migration CAS
# -----------------------------------------------------------------------------
add_executable(test_user_storage_metadata_persisted
        tests/cluster/test_user_storage_metadata_persisted.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_user_storage_metadata_persisted PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_user_storage_metadata_persisted
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_user_storage_metadata_persisted PRIVATE
        Threads::Threads
)

set_target_properties(test_user_storage_metadata_persisted PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_storage_metadata_persisted
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_storage_metadata_persisted"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_storage_metadata_persisted
)

# -----------------------------------------------------------------------------
# Structural guard: storage allocation/migration use atomic persisted APIs
# -----------------------------------------------------------------------------
add_custom_target(run_test_storage_metadata_persisted_callers
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_metadata_persisted_callers.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Director -> Storage user projection builder
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_builder STATIC
        server/src/cluster/storage_user_projection_builder.cpp
        server/src/cluster/storage_user_projection_builder.h
)

target_include_directories(pqnas_storage_user_projection_builder PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_builder
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_builder PUBLIC
        pqnas_cluster_identity_validation
        pqnas_storage_path_validation
)


# -----------------------------------------------------------------------------
# Test: Director -> Storage user projection builder
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_builder
        tests/cluster/test_storage_user_projection_builder.cpp
)

target_include_directories(test_storage_user_projection_builder PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_builder
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_builder PRIVATE
        pqnas_storage_user_projection_builder
)

set_target_properties(test_storage_user_projection_builder PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_builder
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_builder"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_builder
)


# -----------------------------------------------------------------------------
# Director -> Storage user projection synchronization client
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_sync_client STATIC
        server/src/cluster/storage_user_projection_sync_client.cpp
        server/src/cluster/storage_user_projection_sync_client.h
)

target_include_directories(pqnas_storage_user_projection_sync_client PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_sync_client
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_sync_client PUBLIC
        pqnas_node_credential_registry
        pqnas_cluster_identity_validation
        pqnas_storage_path_validation
)


# -----------------------------------------------------------------------------
# Director -> Storage user projection audit client (RT-008)
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_audit_client STATIC
        server/src/cluster/storage_user_projection_audit_client.cpp
        server/src/cluster/storage_user_projection_audit_client.h
)

target_include_directories(pqnas_storage_user_projection_audit_client PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_audit_client
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_audit_client PUBLIC
        pqnas_storage_user_projection_audit
        pqnas_node_credential_registry
        pqnas_cluster_identity_validation
)


# -----------------------------------------------------------------------------
# Cluster Node TLS SPKI identity
# -----------------------------------------------------------------------------
add_library(pqnas_node_tls_identity STATIC
        server/src/cluster/node_tls_identity.cpp
        server/src/cluster/node_tls_identity.h
)

target_include_directories(
        pqnas_node_tls_identity
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_tls_identity
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_node_tls_identity
        PUBLIC
        OpenSSL::Crypto
)


# -----------------------------------------------------------------------------
# Shared pinned HTTPS client for authenticated Director -> Storage traffic
# -----------------------------------------------------------------------------
add_library(pqnas_node_tls_https_client STATIC
        server/src/cluster/node_tls_https_client.cpp
        server/src/cluster/node_tls_https_client.h
)

target_include_directories(
        pqnas_node_tls_https_client
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_compile_definitions(
        pqnas_node_tls_https_client
        PUBLIC
        CPPHTTPLIB_OPENSSL_SUPPORT
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_tls_https_client
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_node_tls_https_client
        PUBLIC
        pqnas_node_tls_identity
        pqnas_cluster_identity_validation
        OpenSSL::SSL
        OpenSSL::Crypto
)


# Heartbeat HTTPS is compiled into pqnas_server. Link the mandatory pinned
# client only after its CMake target exists.
target_link_libraries(
        pqnas_server
        PRIVATE
        pqnas_node_tls_https_client
)


# RT014: every Storage enrollment layer that validates or propagates the
# canonical TLS SPKI identity depends explicitly on the shared primitive.
target_link_libraries(
        pqnas_storage_node_provisioning_store
        PUBLIC
        pqnas_node_tls_identity
)

target_link_libraries(
        pqnas_storage_node_enrollment_client
        PUBLIC
        pqnas_node_tls_identity
)

target_link_libraries(
        pqnas_storage_node_enrollment_https
        PUBLIC
        pqnas_node_tls_https_client
)

target_link_libraries(
        pqnas_storage_node_enrollment_runtime
        PUBLIC
        pqnas_node_tls_identity
)


# -----------------------------------------------------------------------------
# Test: Cluster Node TLS SPKI identity
# -----------------------------------------------------------------------------
add_executable(test_node_tls_identity
        tests/cluster/test_node_tls_identity.cpp
)

target_include_directories(
        test_node_tls_identity
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_node_tls_identity
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        test_node_tls_identity
        PRIVATE
        pqnas_node_tls_identity
        OpenSSL::Crypto
)

set_target_properties(
        test_node_tls_identity
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_node_tls_identity
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_node_tls_identity"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_tls_identity
)


# -----------------------------------------------------------------------------
# Director Node TLS trust store
# -----------------------------------------------------------------------------
add_library(pqnas_node_tls_trust_store STATIC
        server/src/cluster/node_tls_trust_store.cpp
        server/src/cluster/node_tls_trust_store.h
)

target_include_directories(
        pqnas_node_tls_trust_store
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_tls_trust_store
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_node_tls_trust_store
        PUBLIC
        pqnas_node_tls_identity
        pqnas_cluster_identity_validation
)

# RT014 lifecycle: deleting a configured Node must also revoke its durable
# TLS identity before NodeRegistry is removed.
target_link_libraries(
        pqnas_node_deletion_service
        PUBLIC
        pqnas_node_tls_trust_store
)


# RT014: credential rotation must resolve immutable TLS trust before any OLD
# bearer is materialized.
target_link_libraries(
        pqnas_node_credential_rotation_service
        PUBLIC
        pqnas_node_tls_trust_store
)


# RT014: projection synchronization must resolve durable node TLS trust before
# it is able to obtain or transport any per-node bearer credential.
target_link_libraries(
        pqnas_storage_user_projection_sync_client
        PUBLIC
        pqnas_node_tls_trust_store
)

# RT-008 audit client follows the same trust-before-bearer rule as projection
# synchronization. Keep this dependency explicit.
target_link_libraries(
        pqnas_storage_user_projection_audit_client
        PUBLIC
        pqnas_node_tls_trust_store
)


# Heartbeat trust lookup is mandatory and the test target must depend on the
# actual trust-store target rather than an unresolved library-name string.
target_link_libraries(
        test_node_heartbeat_worker
        PRIVATE
        pqnas_node_tls_trust_store
)


# Enrollment consumption must not be linkable without the durable TLS trust
# implementation used before Node registration.
target_link_libraries(
        pqnas_node_enrollment_consumption_service
        PUBLIC
        pqnas_node_tls_trust_store
)


# -----------------------------------------------------------------------------
# Test: Director Node TLS trust store
# -----------------------------------------------------------------------------
add_executable(test_node_tls_trust_store
        tests/cluster/test_node_tls_trust_store.cpp
)

target_include_directories(
        test_node_tls_trust_store
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_node_tls_trust_store
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        test_node_tls_trust_store
        PRIVATE
        pqnas_node_tls_trust_store
)

if (
    CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
    CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
)
    # Security regression only: inject one parent-directory fsync failure
    # after the immutable TLS binding becomes visible.
    target_link_options(
        test_node_tls_trust_store
        PRIVATE
        "-Wl,--wrap=fsync"
    )
endif()

set_target_properties(
        test_node_tls_trust_store
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_node_tls_trust_store
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_node_tls_trust_store"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_tls_trust_store
)


# -----------------------------------------------------------------------------
# Director -> Storage Node credential rotation HTTPS transport
# -----------------------------------------------------------------------------
add_library(pqnas_node_credential_rotation_https STATIC
        server/src/cluster/node_credential_rotation_https.cpp
        server/src/cluster/node_credential_rotation_https.h
)

target_include_directories(
        pqnas_node_credential_rotation_https
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(
        pqnas_node_credential_rotation_https
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_compile_definitions(
        pqnas_node_credential_rotation_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_node_credential_rotation_https
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_node_credential_rotation_https
        PUBLIC
        pqnas_node_credential_rotation_service
        pqnas_node_tls_https_client
        OpenSSL::SSL
        OpenSSL::Crypto
)


# -----------------------------------------------------------------------------
# Director -> Storage user projection HTTPS transport
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_sync_https STATIC
        server/src/cluster/storage_user_projection_sync_https.cpp
        server/src/cluster/storage_user_projection_sync_https.h
)

target_include_directories(pqnas_storage_user_projection_sync_https PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(pqnas_storage_user_projection_sync_https PRIVATE
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_compile_definitions(
        pqnas_storage_user_projection_sync_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_sync_https
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_sync_https PUBLIC
        pqnas_storage_user_projection_sync_client
        pqnas_node_tls_https_client
        OpenSSL::SSL
        OpenSSL::Crypto
)


# -----------------------------------------------------------------------------
# Director -> Storage user projection audit HTTPS transport (RT-008)
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_audit_https STATIC
        server/src/cluster/storage_user_projection_audit_https.cpp
        server/src/cluster/storage_user_projection_audit_https.h
)

target_include_directories(pqnas_storage_user_projection_audit_https PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(pqnas_storage_user_projection_audit_https PRIVATE
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_compile_definitions(
        pqnas_storage_user_projection_audit_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_audit_https
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_audit_https PUBLIC
        pqnas_storage_user_projection_audit
        pqnas_node_tls_https_client
        OpenSSL::SSL
        OpenSSL::Crypto
)


# -----------------------------------------------------------------------------
# Test: strict security-protocol JSON parsing
# -----------------------------------------------------------------------------
add_executable(test_strict_json_parse
        tests/cluster/test_strict_json_parse.cpp
)

target_include_directories(test_strict_json_parse PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_strict_json_parse
        PRIVATE
        -Werror
    )
endif()

set_target_properties(test_strict_json_parse PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_strict_json_parse
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_strict_json_parse"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_strict_json_parse
)


# -----------------------------------------------------------------------------
# Director -> Storage browser handoff ticket client
# -----------------------------------------------------------------------------
add_library(pqnas_storage_handoff_ticket_client STATIC
        server/src/cluster/storage_handoff_ticket_client.cpp
        server/src/cluster/storage_handoff_ticket_client.h
)

target_include_directories(pqnas_storage_handoff_ticket_client PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_handoff_ticket_client
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_handoff_ticket_client PUBLIC
        pqnas_node_credential_registry
        pqnas_node_tls_trust_store
        pqnas_cluster_identity_validation
        pqnas_storage_handoff_ticket_store
)


# -----------------------------------------------------------------------------
# Director -> Storage browser handoff ticket HTTPS transport
# -----------------------------------------------------------------------------
add_library(pqnas_storage_handoff_ticket_https STATIC
        server/src/cluster/storage_handoff_ticket_https.cpp
        server/src/cluster/storage_handoff_ticket_https.h
)

target_include_directories(pqnas_storage_handoff_ticket_https PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(pqnas_storage_handoff_ticket_https PRIVATE
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_compile_definitions(
        pqnas_storage_handoff_ticket_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_handoff_ticket_https
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_handoff_ticket_https PUBLIC
        pqnas_storage_handoff_ticket_client
        pqnas_node_tls_https_client
        pqnas_storage_handoff_ticket_store
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Director production composition. Browser-facing route registration remains
# separate; this only exposes the hardened handoff client to pqnas_server.
target_link_libraries(pqnas_server PRIVATE
        pqnas_storage_handoff_ticket_https
)


# -----------------------------------------------------------------------------
# Director browser handoff start route
# -----------------------------------------------------------------------------
add_library(pqnas_director_storage_handoff_start_routes STATIC
        server/src/routes/routes_director_storage_handoff_start.cpp
        server/src/routes/routes_director_storage_handoff_start.h
)

target_include_directories(
        pqnas_director_storage_handoff_start_routes
        PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

target_include_directories(
        pqnas_director_storage_handoff_start_routes
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_director_storage_handoff_start_routes
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        pqnas_director_storage_handoff_start_routes
        PUBLIC
        pqnas_storage_handoff_ticket_client
        pqnas_storage_handoff_ticket_store
        pqnas_cluster_identity_validation
)


# -----------------------------------------------------------------------------
# Test: Director browser handoff start route
# -----------------------------------------------------------------------------
add_executable(test_director_storage_handoff_start_route
        tests/cluster/test_director_storage_handoff_start_route.cpp
)

target_include_directories(
        test_director_storage_handoff_start_route
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_director_storage_handoff_start_route
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        test_director_storage_handoff_start_route
        PRIVATE
        pqnas_director_storage_handoff_start_routes
)

set_target_properties(
        test_director_storage_handoff_start_route
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_director_storage_handoff_start_route
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_director_storage_handoff_start_route"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_director_storage_handoff_start_route
)


# -----------------------------------------------------------------------------
# Test: Director -> Storage browser handoff ticket client
# -----------------------------------------------------------------------------
add_executable(test_storage_handoff_ticket_client
        tests/cluster/test_storage_handoff_ticket_client.cpp
        server/src/cluster/node_registry.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_storage_handoff_ticket_client PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_handoff_ticket_client PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_handoff_ticket_client
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_handoff_ticket_client PRIVATE
        pqnas_storage_handoff_ticket_client
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_storage_handoff_ticket_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_handoff_ticket_client
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_handoff_ticket_client"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_handoff_ticket_client
)


# -----------------------------------------------------------------------------
# Test: Director -> Storage browser handoff ticket HTTPS protocol
# -----------------------------------------------------------------------------
add_executable(test_storage_handoff_ticket_https
        tests/cluster/test_storage_handoff_ticket_https.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_storage_handoff_ticket_https PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_definitions(
        test_storage_handoff_ticket_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

target_compile_options(test_storage_handoff_ticket_https PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_handoff_ticket_https
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_handoff_ticket_https PRIVATE
        pqnas_storage_handoff_ticket_https
        ${SODIUM_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
)

set_target_properties(test_storage_handoff_ticket_https PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_handoff_ticket_https
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_handoff_ticket_https"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_handoff_ticket_https
)

add_custom_target(run_test_storage_handoff_ticket_client_guards
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_handoff_ticket_client_guards.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Test: Director -> Storage projection synchronization client
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_sync_client
        tests/cluster/test_storage_user_projection_sync_client.cpp
        server/src/cluster/node_registry.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_storage_user_projection_sync_client PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_user_projection_sync_client PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_sync_client
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_sync_client PRIVATE
        pqnas_storage_user_projection_sync_client
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_storage_user_projection_sync_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_sync_client
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_sync_client"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_sync_client
)


# -----------------------------------------------------------------------------
# Test: Director -> Storage projection audit client
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_audit_client
        tests/cluster/test_storage_user_projection_audit_client.cpp
        server/src/cluster/node_registry.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_storage_user_projection_audit_client PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_storage_user_projection_audit_client PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_audit_client
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_audit_client PRIVATE
        pqnas_storage_user_projection_audit_client
        ${SODIUM_LIBRARIES}
        Threads::Threads
)

set_target_properties(test_storage_user_projection_audit_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_audit_client
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_audit_client"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_audit_client
)


# -----------------------------------------------------------------------------
# Test: Node credential rotation HTTPS protocol parser
# -----------------------------------------------------------------------------
add_executable(test_node_credential_rotation_https
        tests/cluster/test_node_credential_rotation_https.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(
        test_node_credential_rotation_https
        PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_definitions(
        test_node_credential_rotation_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

target_compile_options(
        test_node_credential_rotation_https
        PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_node_credential_rotation_https
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(
        test_node_credential_rotation_https
        PRIVATE
        pqnas_node_credential_rotation_https
        ${SODIUM_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
)

set_target_properties(
        test_node_credential_rotation_https
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(
        run_test_node_credential_rotation_https
        COMMAND
        "${CMAKE_BINARY_DIR}/bin/test_node_credential_rotation_https"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_node_credential_rotation_https
)

add_custom_target(
        run_test_node_credential_rotation_https_guards
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_node_credential_rotation_https_guards.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Test: projection synchronization HTTPS protocol parser
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_sync_https
        tests/cluster/test_storage_user_projection_sync_https.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_storage_user_projection_sync_https PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_definitions(
        test_storage_user_projection_sync_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

target_compile_options(test_storage_user_projection_sync_https PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_sync_https
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_sync_https PRIVATE
        pqnas_storage_user_projection_sync_https
        ${SODIUM_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
)

set_target_properties(test_storage_user_projection_sync_https PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_sync_https
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_sync_https"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_sync_https
)


# -----------------------------------------------------------------------------
# Test: projection audit HTTPS protocol parser
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_audit_https
        tests/cluster/test_storage_user_projection_audit_https.cpp
        server/src/cluster/internal_auth.cpp
)

target_include_directories(test_storage_user_projection_audit_https PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_definitions(
        test_storage_user_projection_audit_https
        PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT
)

target_compile_options(test_storage_user_projection_audit_https PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_audit_https
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_audit_https PRIVATE
        pqnas_storage_user_projection_audit_https
        ${SODIUM_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
)

set_target_properties(test_storage_user_projection_audit_https PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_audit_https
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_audit_https"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_audit_https
)


# -----------------------------------------------------------------------------
# Structural guard: projection synchronization HTTPS hardening
# -----------------------------------------------------------------------------
add_custom_target(run_test_storage_user_projection_sync_https_guards
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_user_projection_sync_https_guards.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Structural guard: Storage projection durability uncertainty
# -----------------------------------------------------------------------------
add_custom_target(run_test_storage_projection_durability_guard
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_projection_durability_guard.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Structural guard: authoritative users.json crash durability
# -----------------------------------------------------------------------------
add_custom_target(run_test_users_registry_durability_guard
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_users_registry_durability_guard.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Director persistent Storage projection synchronization state
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_sync_state_store STATIC
        server/src/cluster/storage_user_projection_sync_state_store.cpp
        server/src/cluster/storage_user_projection_sync_state_store.h
)

target_include_directories(pqnas_storage_user_projection_sync_state_store PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_sync_state_store
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_sync_state_store PUBLIC
        pqnas_cluster_identity_validation
)


# RT010: Node deletion must fail closed while durable confirmed Storage
# projection state still references the Node being removed.
target_link_libraries(pqnas_node_deletion_service PUBLIC
        pqnas_storage_user_projection_sync_state_store
)


# -----------------------------------------------------------------------------
# Test: Director persistent projection synchronization state
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_sync_state_store
        tests/cluster/test_storage_user_projection_sync_state_store.cpp
)

target_include_directories(test_storage_user_projection_sync_state_store PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_sync_state_store
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_sync_state_store PRIVATE
        pqnas_storage_user_projection_sync_state_store
        Threads::Threads
)

set_target_properties(test_storage_user_projection_sync_state_store PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_sync_state_store
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_sync_state_store"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_sync_state_store
)


# -----------------------------------------------------------------------------
# Structural guard: persistent projection synchronization state
# -----------------------------------------------------------------------------
add_custom_target(run_test_storage_user_projection_sync_state_store_guards
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_user_projection_sync_state_store_guards.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# RT-003: Director startup consistency for users.json vs confirmed projections
# -----------------------------------------------------------------------------
target_sources(pqnas_server PRIVATE
        server/src/cluster/storage_projection_startup_consistency.cpp
        server/src/cluster/cluster_safe_user_delete.cpp
)

add_executable(test_storage_projection_startup_consistency
        tests/cluster/test_storage_projection_startup_consistency.cpp
        server/src/cluster/storage_projection_startup_consistency.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_storage_projection_startup_consistency PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_projection_startup_consistency
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_projection_startup_consistency PRIVATE
        pqnas_storage_user_projection_sync_state_store
        Threads::Threads
)

set_target_properties(test_storage_projection_startup_consistency PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_projection_startup_consistency
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_projection_startup_consistency"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_projection_startup_consistency
)

add_custom_target(run_test_storage_projection_startup_consistency_guard
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_projection_startup_consistency_guard.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Installer test: explicit Director projection sync-state bootstrap
# -----------------------------------------------------------------------------
add_custom_target(run_test_pqnas_director_projection_state
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/installer/test_pqnas_director_projection_state.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Structural guard: Director sync-state bootstrap trust boundary
# -----------------------------------------------------------------------------
add_custom_target(run_test_storage_projection_sync_state_bootstrap_guards
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_projection_sync_state_bootstrap_guards.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Test: persisted cluster-safe user deletion
# -----------------------------------------------------------------------------
add_executable(test_user_delete_persisted
        tests/cluster/test_user_delete_persisted.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_user_delete_persisted PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_user_delete_persisted
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_user_delete_persisted PRIVATE
        Threads::Threads
)

set_target_properties(test_user_delete_persisted PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_user_delete_persisted
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_user_delete_persisted"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_user_delete_persisted
)


# -----------------------------------------------------------------------------
# RT-004: Director cluster-safe physical user deletion
# -----------------------------------------------------------------------------
add_executable(test_cluster_safe_user_delete
        tests/cluster/test_cluster_safe_user_delete.cpp
        server/src/cluster/cluster_safe_user_delete.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_cluster_safe_user_delete PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_cluster_safe_user_delete
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_cluster_safe_user_delete PRIVATE
        pqnas_storage_user_projection_sync_state_store
        Threads::Threads
)

set_target_properties(test_cluster_safe_user_delete PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_cluster_safe_user_delete
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_cluster_safe_user_delete"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_cluster_safe_user_delete
)


# -----------------------------------------------------------------------------
# Structural guard: user deletion stays persisted + cluster-safe
# -----------------------------------------------------------------------------
add_custom_target(run_test_user_delete_persisted_callers
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_user_delete_persisted_callers.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Director Storage user projection reconciliation
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_reconciler STATIC
        server/src/cluster/storage_user_projection_reconciler.cpp
        server/src/cluster/storage_user_projection_reconciler.h
)

target_include_directories(pqnas_storage_user_projection_reconciler PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_reconciler
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_reconciler PUBLIC
        pqnas_storage_user_projection_builder
        pqnas_storage_user_projection_sync_state_store
        pqnas_storage_user_projection_audit
)


# -----------------------------------------------------------------------------
# Test: deterministic Director projection reconciliation
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_reconciler
        tests/cluster/test_storage_user_projection_reconciler.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_storage_user_projection_reconciler PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_reconciler
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_reconciler PRIVATE
        pqnas_storage_user_projection_reconciler
        Threads::Threads
)

set_target_properties(test_storage_user_projection_reconciler PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_reconciler
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_reconciler"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_reconciler
)


# -----------------------------------------------------------------------------
# Structural guard: deterministic projection reconciliation
# -----------------------------------------------------------------------------
add_custom_target(run_test_storage_user_projection_reconciler_guards
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_user_projection_reconciler_guards.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Director Storage projection reconciliation worker
# -----------------------------------------------------------------------------
add_library(pqnas_storage_user_projection_reconcile_worker STATIC
        server/src/cluster/storage_user_projection_reconcile_worker.cpp
        server/src/cluster/storage_user_projection_reconcile_worker.h
)

target_include_directories(pqnas_storage_user_projection_reconcile_worker PUBLIC
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        pqnas_storage_user_projection_reconcile_worker
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(pqnas_storage_user_projection_reconcile_worker PUBLIC
        pqnas_storage_user_projection_reconciler
        Threads::Threads
)


# -----------------------------------------------------------------------------
# Test: Storage projection reconciliation worker lifecycle
# -----------------------------------------------------------------------------
add_executable(test_storage_user_projection_reconcile_worker
        tests/cluster/test_storage_user_projection_reconcile_worker.cpp
)

target_include_directories(test_storage_user_projection_reconcile_worker PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_storage_user_projection_reconcile_worker
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_storage_user_projection_reconcile_worker PRIVATE
        pqnas_storage_user_projection_reconcile_worker
        Threads::Threads
)

set_target_properties(test_storage_user_projection_reconcile_worker PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_storage_user_projection_reconcile_worker
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_storage_user_projection_reconcile_worker"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_storage_user_projection_reconcile_worker
)


# -----------------------------------------------------------------------------
# Structural guard: projection reconciliation worker remains only scheduling
# -----------------------------------------------------------------------------
add_custom_target(run_test_storage_user_projection_reconcile_worker_guards
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_user_projection_reconcile_worker_guards.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Director production Storage projection reconciliation composition
# -----------------------------------------------------------------------------
target_link_libraries(pqnas_server PRIVATE
        pqnas_node_credential_rotation_https
        pqnas_storage_user_projection_sync_https
        pqnas_storage_user_projection_audit_client
        pqnas_storage_user_projection_audit_https
        pqnas_storage_user_projection_reconcile_worker
)

# -----------------------------------------------------------------------------
# Structural guard: Director Node credential rotation production composition
# -----------------------------------------------------------------------------
add_custom_target(run_test_node_credential_rotation_director_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_node_credential_rotation_director_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# -----------------------------------------------------------------------------
# Structural guard: Director projection reconciliation production composition
# -----------------------------------------------------------------------------
add_custom_target(run_test_storage_user_projection_reconcile_production_wiring
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/test_storage_user_projection_reconcile_production_wiring.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# -----------------------------------------------------------------------------
# Test: test_admin_user_delete_auth_cleanup
# -----------------------------------------------------------------------------
add_executable(test_admin_user_delete_auth_cleanup
        tests/cluster/test_admin_user_delete_auth_cleanup.cpp
        server/src/routes/routes_admin_user_lifecycle.cpp
        server/src/users_registry.cpp
)

target_include_directories(test_admin_user_delete_auth_cleanup PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)

target_link_libraries(test_admin_user_delete_auth_cleanup PRIVATE
        Threads::Threads
)

set_target_properties(test_admin_user_delete_auth_cleanup PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_admin_user_delete_auth_cleanup
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_admin_user_delete_auth_cleanup"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_admin_user_delete_auth_cleanup
)

# -----------------------------------------------------------------------------
# Test: test_auth_credentials_fingerprint_erase
# -----------------------------------------------------------------------------
add_executable(test_auth_credentials_fingerprint_erase
        tests/cluster/test_auth_credentials_fingerprint_erase.cpp
        server/src/opaque_credentials.cpp
        server/src/password_credentials.cpp
        server/src/durable_file.cpp
)

target_include_directories(test_auth_credentials_fingerprint_erase PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_auth_credentials_fingerprint_erase PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(test_auth_credentials_fingerprint_erase PRIVATE
        Threads::Threads
        ${SODIUM_LIBRARIES}
)

set_target_properties(test_auth_credentials_fingerprint_erase PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_auth_credentials_fingerprint_erase
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_auth_credentials_fingerprint_erase"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_auth_credentials_fingerprint_erase
)

# -----------------------------------------------------------------------------
# Test: test_auth_credentials_stale_write
# -----------------------------------------------------------------------------
add_executable(test_auth_credentials_stale_write
        tests/cluster/test_auth_credentials_stale_write.cpp
        server/src/opaque_credentials.cpp
        server/src/password_credentials.cpp
        server/src/durable_file.cpp
)

target_include_directories(test_auth_credentials_stale_write PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${SODIUM_INCLUDE_DIRS}
)

target_compile_options(test_auth_credentials_stale_write PRIVATE
        ${SODIUM_CFLAGS_OTHER}
)

target_link_libraries(test_auth_credentials_stale_write PRIVATE
        Threads::Threads
        ${SODIUM_LIBRARIES}
)

set_target_properties(test_auth_credentials_stale_write PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_auth_credentials_stale_write
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_auth_credentials_stale_write"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_auth_credentials_stale_write
)

# -----------------------------------------------------------------------------
# FINAL-002: workspace external-session interactive-login role gate
# -----------------------------------------------------------------------------
add_executable(test_workspace_external_session_role_gate
        tests/cluster/test_workspace_external_session_role_gate.cpp
        server/src/routes/routes_workspace_external_sessions.cpp
        server/src/workspace_external_sessions.cpp
        server/src/workspaces.cpp
        server/src/interactive_user_login_policy.cc
)

target_include_directories(test_workspace_external_session_role_gate PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/include
        ${CMAKE_SOURCE_DIR}/server/third_party
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        test_workspace_external_session_role_gate
        PRIVATE
        -Werror
    )
endif()

target_link_libraries(test_workspace_external_session_role_gate PRIVATE
        Threads::Threads
)

set_target_properties(test_workspace_external_session_role_gate PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_workspace_external_session_role_gate
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_workspace_external_session_role_gate"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_workspace_external_session_role_gate
)

# -----------------------------------------------------------------------------
# NEW-007: aggregate Python security/installer regression suite
#
# Discovery happens at execution time so adding another test_*.py guard does
# not require maintaining a second hand-written CMake dependency census.
# -----------------------------------------------------------------------------
add_custom_target(run_all_cluster_security_guards
        COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/cluster/run_all_security_python.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
