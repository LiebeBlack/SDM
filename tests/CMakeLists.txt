# ==============================================================================
#  S-DOWNLOAD MANAGER (SDM) - tests/CMakeLists.txt
#
#  Suite de pruebas unitarias (CTest). Enlazan sdm_core: lógica pura de Models
#  y persistencia SQLite en fichero temporal. Sin red ni GUI (QTEST_GUILESS).
# ==============================================================================

find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

add_executable(sdm_test_models test_models.cpp)
target_link_libraries(sdm_test_models PRIVATE sdm_core Qt6::Test)

add_executable(sdm_test_database test_database.cpp)
target_link_libraries(sdm_test_database PRIVATE sdm_core Qt6::Test)

add_test(NAME models COMMAND sdm_test_models)
add_test(NAME database COMMAND sdm_test_database)

set_tests_properties(models database PROPERTIES TIMEOUT 120)
