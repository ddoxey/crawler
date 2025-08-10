include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_search_module(LUA REQUIRED lua)

# ----------------- URL tests -----------------
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
    ${OPENSSL_LIBRARIES}
    pthread
)

# ----------------- LuaProcessor tests -----------------
add_executable(test_luaprocessor
    test_luaprocessor.cpp
    "${PROJECT_SOURCE_DIR}/src/URL.cpp"
    "${PROJECT_SOURCE_DIR}/src/LuaProcessor.cpp"       # <-- add this
)

target_include_directories(test_luaprocessor
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/third_party/sol2/include"    # <-- sol2 headers
    ${LUA_INCLUDE_DIRS}                                 # <-- Lua headers
)

target_link_libraries(test_luaprocessor
  PRIVATE
    GTest::gtest
    GTest::gtest_main
    ${OPENSSL_LIBRARIES}
    ${LUA_LIBRARIES}                                    # <-- link Lua
    pthread
    stdc++fs
)

# Register tests (call once per target)
gtest_discover_tests(test_url)
gtest_discover_tests(test_luaprocessor)
