set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/tests_HoRGod)

add_executable(io_test io.cpp)
target_link_libraries(io_test Boost::unit_test_framework Threads::Threads EMPTool HoRGod HoRGod)

add_executable(sharing_test sharing.cpp)
target_link_libraries(sharing_test Boost::unit_test_framework Threads::Threads HoRGod HoRGod)

add_executable(utils_test utils.cpp)
target_link_libraries(utils_test Boost::unit_test_framework Threads::Threads HoRGod HoRGod)

add_executable(offline_test offline.cpp)
target_link_libraries(offline_test Boost::unit_test_framework Threads::Threads HoRGod NTL GMP HoRGod)

add_executable(jump_test jump.cpp)
target_link_libraries(jump_test Boost::unit_test_framework Threads::Threads HoRGod)

add_executable(ijmp_test ijmp.cpp)
target_link_libraries(ijmp_test Boost::unit_test_framework Threads::Threads HoRGod HoRGod)

add_executable(online_test online.cpp)
target_link_libraries(online_test Boost::unit_test_framework Threads::Threads HoRGod)

add_executable(offline_online_test offline_online.cpp)
target_link_libraries(offline_online_test Boost::unit_test_framework Threads::Threads HoRGod)

add_custom_target(tests_HoRGod)
add_dependencies(tests_HoRGod
    io_test sharing_test utils_test offline_test jump_test online_test)

add_test(NAME io_test COMMAND io_test)
add_test(NAME sharing_test COMMAND sharing_test)
add_test(NAME utils_test COMMAND utils_test)
add_test(NAME offline_test COMMAND offline_test)
add_test(NAME jump_test COMMAND jump_test)
add_test(NAME ijmp_test COMMAND ijmp_test)
add_test(NAME online_test COMMAND online_test)
add_test(NAME offline_online_test COMMAND offline_online_test)