include(FetchContent)

FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
set(gtest_force_shared_crt ON CACHE INTERNAL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(avox_tests
  test_version.cpp)
target_link_libraries(avox_tests PRIVATE avox::core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(avox_tests)
