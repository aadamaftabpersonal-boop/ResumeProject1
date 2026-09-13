cmake_minimum_required(VERSION 3.20)
project(distributed_db_engine VERSION 0.1.0 LANGUAGES CXX)

option(DDB_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ddb_storage
  src/storage/page_manager.cpp
  src/storage/log_manager.cpp
  src/storage/physical_mutation.cpp
  src/recovery/recovery_analysis.cpp
  src/recovery/replay_context.cpp
  src/recovery/redo_engine.cpp
  src/buffer/buffer_pool_manager.cpp
  src/database.cpp
  src/index/bplus_tree.cpp
  src/execution/table_heap.cpp
  src/concurrency/lock_manager.cpp
  src/concurrency/transaction.cpp
  src/concurrency/transaction_coordinator.cpp
)
target_include_directories(ddb_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
  target_compile_options(ddb_storage PRIVATE /W4 /permissive- /EHsc)
  if(DDB_ENABLE_SANITIZERS)
    message(STATUS "Enabling AddressSanitizer with MSVC; UndefinedBehaviorSanitizer is unavailable on MSVC.")
    target_compile_options(ddb_storage PRIVATE /fsanitize=address)
  endif()
else()
  target_compile_options(ddb_storage PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(DDB_ENABLE_SANITIZERS)
    target_compile_options(ddb_storage PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ddb_storage PUBLIC -fsanitize=address,undefined)
  endif()
endif()

enable_testing()
add_executable(storage_tests tests/storage_tests.cpp)
target_link_libraries(storage_tests PRIVATE ddb_storage)
if(DDB_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(storage_tests PRIVATE /fsanitize=address)
  else()
    target_compile_options(storage_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(storage_tests PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME storage_tests COMMAND storage_tests)
set_tests_properties(storage_tests PROPERTIES LABELS "unit;integration")

add_executable(buffer_pool_tests tests/buffer_pool_tests.cpp)
target_link_libraries(buffer_pool_tests PRIVATE ddb_storage)
if(DDB_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(buffer_pool_tests PRIVATE /fsanitize=address)
  else()
    target_compile_options(buffer_pool_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(buffer_pool_tests PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME buffer_pool_tests COMMAND buffer_pool_tests)
set_tests_properties(buffer_pool_tests PROPERTIES LABELS "unit;integration;stress")

add_executable(bplus_tree_tests tests/bplus_tree_tests.cpp)
target_link_libraries(bplus_tree_tests PRIVATE ddb_storage)
if(DDB_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(bplus_tree_tests PRIVATE /fsanitize=address)
  else()
    target_compile_options(bplus_tree_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(bplus_tree_tests PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME bplus_tree_tests COMMAND bplus_tree_tests)
set_tests_properties(bplus_tree_tests PROPERTIES LABELS "unit;integration;stress")

add_executable(execution_tests tests/execution_tests.cpp)
target_link_libraries(execution_tests PRIVATE ddb_storage)
if(DDB_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(execution_tests PRIVATE /fsanitize=address)
  else()
    target_compile_options(execution_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(execution_tests PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME execution_tests COMMAND execution_tests)
set_tests_properties(execution_tests PROPERTIES LABELS "unit;integration;stress;randomized")

add_executable(transaction_tests tests/transaction_tests.cpp)
target_link_libraries(transaction_tests PRIVATE ddb_storage)
if(DDB_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(transaction_tests PRIVATE /fsanitize=address)
  else()
    target_compile_options(transaction_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(transaction_tests PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME transaction_tests COMMAND transaction_tests)
set_tests_properties(transaction_tests PROPERTIES LABELS "unit;integration;stress;concurrency")

add_executable(page_format_tests tests/page_format_tests.cpp)
target_link_libraries(page_format_tests PRIVATE ddb_storage)
add_test(NAME page_format_tests COMMAND page_format_tests)
set_tests_properties(page_format_tests PROPERTIES LABELS "unit;integration;storage")

add_executable(mutation_infrastructure_tests tests/mutation_infrastructure_tests.cpp)
target_link_libraries(mutation_infrastructure_tests PRIVATE ddb_storage)
add_test(NAME mutation_infrastructure_tests COMMAND mutation_infrastructure_tests)

add_executable(bplus_mutation_tests tests/bplus_mutation_tests.cpp)
target_link_libraries(bplus_mutation_tests PRIVATE ddb_storage)
add_test(NAME bplus_mutation_tests COMMAND bplus_mutation_tests)

add_executable(tableheap_mutation_tests tests/tableheap_mutation_tests.cpp)
target_link_libraries(tableheap_mutation_tests PRIVATE ddb_storage)
add_test(NAME tableheap_mutation_tests COMMAND tableheap_mutation_tests)

add_executable(wal_architecture_tests tests/wal_architecture_tests.cpp)
target_link_libraries(wal_architecture_tests PRIVATE ddb_storage)
add_test(NAME wal_architecture_tests COMMAND wal_architecture_tests)

add_executable(wal_format_tests tests/wal_format_tests.cpp)
target_link_libraries(wal_format_tests PRIVATE ddb_storage)
add_test(NAME wal_format_tests COMMAND wal_format_tests)

add_executable(wal_transaction_tests tests/wal_transaction_tests.cpp)
target_link_libraries(wal_transaction_tests PRIVATE ddb_storage)
add_test(NAME wal_transaction_tests COMMAND wal_transaction_tests)

add_executable(wal_allocation_tests tests/wal_allocation_tests.cpp)
target_link_libraries(wal_allocation_tests PRIVATE ddb_storage)
add_test(NAME wal_allocation_tests COMMAND wal_allocation_tests)
add_executable(transactional_allocation_tests tests/transactional_allocation_tests.cpp)
target_link_libraries(transactional_allocation_tests PRIVATE ddb_storage)
add_test(NAME transactional_allocation_tests COMMAND transactional_allocation_tests)

add_executable(page_allocation_state_tests tests/page_allocation_state_tests.cpp)
target_link_libraries(page_allocation_state_tests PRIVATE ddb_storage)
add_test(NAME page_allocation_state_tests COMMAND page_allocation_state_tests)

add_executable(transactional_structure_allocation_tests tests/transactional_structure_allocation_tests.cpp)
target_link_libraries(transactional_structure_allocation_tests PRIVATE ddb_storage)
add_test(NAME transactional_structure_allocation_tests COMMAND transactional_structure_allocation_tests)

add_executable(recovery_analysis_tests tests/recovery_analysis_tests.cpp)
target_link_libraries(recovery_analysis_tests PRIVATE ddb_storage)
add_test(NAME recovery_analysis_tests COMMAND recovery_analysis_tests)

add_executable(replay_infrastructure_tests tests/replay_infrastructure_tests.cpp)
target_link_libraries(replay_infrastructure_tests PRIVATE ddb_storage)
add_test(NAME replay_infrastructure_tests COMMAND replay_infrastructure_tests)

add_executable(redo_tests tests/redo_tests.cpp)
target_link_libraries(redo_tests PRIVATE ddb_storage)
add_test(NAME redo_tests COMMAND redo_tests)

add_executable(redo_structure_tests tests/redo_structure_tests.cpp)
target_link_libraries(redo_structure_tests PRIVATE ddb_storage)
add_test(NAME redo_structure_tests COMMAND redo_structure_tests)

# Every executable which links the ASan-instrumented storage library must be
# instrumented too. Keeping this centralized avoids MSVC annotation-mismatch
# link failures when a new test is added.
if(DDB_ENABLE_SANITIZERS)
  foreach(ddb_test_target
      storage_tests buffer_pool_tests bplus_tree_tests execution_tests
      transaction_tests page_format_tests mutation_infrastructure_tests
      bplus_mutation_tests tableheap_mutation_tests wal_architecture_tests wal_format_tests wal_transaction_tests wal_allocation_tests transactional_allocation_tests page_allocation_state_tests transactional_structure_allocation_tests recovery_analysis_tests replay_infrastructure_tests redo_tests redo_structure_tests)
    if(MSVC)
      target_compile_options(${ddb_test_target} PRIVATE /fsanitize=address)
    else()
      target_compile_options(${ddb_test_target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${ddb_test_target} PRIVATE -fsanitize=address,undefined)
    endif()
  endforeach()
endif()

add_executable(storage_benchmark benchmarks/storage_benchmark.cpp)
target_link_libraries(storage_benchmark PRIVATE ddb_storage)
if(DDB_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(storage_benchmark PRIVATE /fsanitize=address)
  else()
    target_compile_options(storage_benchmark PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(storage_benchmark PRIVATE -fsanitize=address,undefined)
  endif()
endif()

add_executable(bplus_tree_benchmark benchmarks/bplus_tree_benchmark.cpp)
target_link_libraries(bplus_tree_benchmark PRIVATE ddb_storage)
if(DDB_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(bplus_tree_benchmark PRIVATE /fsanitize=address)
  else()
    target_compile_options(bplus_tree_benchmark PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(bplus_tree_benchmark PRIVATE -fsanitize=address,undefined)
  endif()
endif()

add_executable(query_execution_benchmark benchmarks/query_execution_benchmark.cpp)
target_link_libraries(query_execution_benchmark PRIVATE ddb_storage)

add_executable(lock_manager_benchmark benchmarks/lock_manager_benchmark.cpp)
target_link_libraries(lock_manager_benchmark PRIVATE ddb_storage)

add_executable(redo_benchmark benchmarks/redo_benchmark.cpp)
target_link_libraries(redo_benchmark PRIVATE ddb_storage)
