# tests/coop/CMakeLists.txt
set(CoopDir "${CMAKE_SOURCE_DIR}/code/coop")
set(CoopTestFiles main.cpp test_sanity.cpp test_adr.cpp test_reliable.cpp ${CoopDir}/coop_adr.cpp ${CoopDir}/coop_reliable.cpp)
add_executable(CoopTests ${CoopTestFiles})
target_include_directories(CoopTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CoopDir})
add_test(NAME coop COMMAND CoopTests)
