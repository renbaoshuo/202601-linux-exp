cmake_minimum_required(VERSION 3.20)
project(fucode LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

foreach(dependency llama.cpp curl)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dependency}/CMakeLists.txt")
        message(FATAL_ERROR "Missing ${dependency}; run: git submodule update --init --recursive")
    endif()
endforeach()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
foreach(feature
        BUILD_CURL_EXE BUILD_EXAMPLES BUILD_LIBCURL_DOCS BUILD_MISC_DOCS ENABLE_CURL_MANUAL
        CURL_USE_PKGCONFIG CURL_ENABLE_SSL CURL_ZLIB CURL_BROTLI CURL_ZSTD
        CURL_USE_LIBPSL CURL_USE_LIBSSH2 USE_LIBIDN2 USE_NGHTTP2)
    set(${feature} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(third_party/curl EXCLUDE_FROM_ALL)

set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
foreach(feature
        LLAMA_BUILD_TESTS LLAMA_BUILD_EXAMPLES LLAMA_BUILD_TOOLS LLAMA_BUILD_SERVER LLAMA_BUILD_APP
        LLAMA_OPENSSL LLAMA_BUILD_BORINGSSL LLAMA_BUILD_LIBRESSL LLAMA_LLGUIDANCE
        GGML_METAL GGML_CUDA GGML_BLAS GGML_OPENMP)
    set(${feature} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL)

add_executable(fucode fucode.cpp)
target_link_libraries(fucode PRIVATE CURL::libcurl vendor::nlohmann)

add_executable(fucode-server server.cpp)
target_include_directories(fucode-server PRIVATE third_party/llama.cpp/vendor/cpp-httplib)
target_link_libraries(fucode-server PRIVATE llama-common cpp-httplib)

foreach(program fucode fucode-server)
    target_compile_options(${program} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
