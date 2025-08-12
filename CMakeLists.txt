cmake_minimum_required(VERSION 3.10)
project(WebCrawler)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(crawler
    src/main.cpp
    src/UAgent.cpp
    src/URL.cpp
    src/URLManager.cpp
    src/Crawler.cpp
    src/CacheManager.cpp
    src/LuaProcessor.cpp
    src/ResultWriter.cpp
    src/HttpResponse.cpp
    src/Config.cpp
)

target_compile_options(crawler PRIVATE -g)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_search_module(LUA REQUIRED lua)

target_include_directories(crawler
  PRIVATE
    ${LUA_INCLUDE_DIRS}
    third_party/sol2/include
)

target_link_libraries(crawler
  PRIVATE
    ${CURL_LIBRARIES}
    ${LUA_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    stdc++fs
    pthread
)

enable_testing()
add_subdirectory(test)
