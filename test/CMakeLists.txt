include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(googletest)

enable_testing()

include(GoogleTest)

add_executable(test_url
    test_url.cpp
    "${PROJECT_SOURCE_DIR}/src/URL.cpp"
)

target_include_directories(test_url
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(test_url
    PRIVATE
    GTest::gtest
    GTest::gtest_main
    pthread
)

gtest_discover_tests(test_url)
