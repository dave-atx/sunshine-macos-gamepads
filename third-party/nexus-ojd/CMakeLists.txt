cmake_minimum_required(VERSION 3.16)
project(nexus_ojd_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The protocol core has no third-party dependencies: no OJD, no Sunshine, no
# JSON library. That keeps it testable anywhere and keeps the wire format under
# unit test.
add_library(ojd_client STATIC
  src/sha256.cpp
  src/protocol.cpp
  src/reply.cpp
  src/feed_client.cpp
  src/state_mapping.cpp
  src/pad_session.cpp
)

target_include_directories(ojd_client PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
  target_compile_options(ojd_client PRIVATE /W4 /permissive-)
else()
  target_compile_options(ojd_client PRIVATE -Wall -Wextra -Wpedantic)
  find_package(Threads REQUIRED)
  target_link_libraries(ojd_client PUBLIC Threads::Threads)
endif()

# AF_UNIX only exists on Unix-like systems.
if(WIN32)
  target_compile_definitions(ojd_client PUBLIC OJD_NO_UNIX_SOCKET)
endif()

enable_testing()

add_executable(ojd_client_test tests/ojd_client_test.cpp)
target_link_libraries(ojd_client_test PRIVATE ojd_client)
add_test(NAME ojd_client COMMAND ojd_client_test)

# Sunshine adapter. Built only when pointed at a Sunshine checkout, because it
# includes Sunshine's platform/common.h and links into the sunshine target.
set(SUNSHINE_SOURCE_DIR "" CACHE PATH "Path to a Sunshine source checkout")

if(SUNSHINE_SOURCE_DIR)
  if(NOT EXISTS "${SUNSHINE_SOURCE_DIR}/src/platform/common.h")
    message(FATAL_ERROR "SUNSHINE_SOURCE_DIR does not contain src/platform/common.h")
  endif()

  add_library(ojd_sunshine_adapter STATIC src/sunshine_adapter.cpp)
  target_link_libraries(ojd_sunshine_adapter PUBLIC ojd_client)
  target_include_directories(ojd_sunshine_adapter PRIVATE
    ${SUNSHINE_SOURCE_DIR}/src
  )
  message(STATUS "Building Sunshine adapter against ${SUNSHINE_SOURCE_DIR}")
else()
  message(STATUS "SUNSHINE_SOURCE_DIR not set; skipping the Sunshine adapter target")
endif()

# The Sunshine bridge itself (not the adapter TU) is portable: it only needs the
# core. It is added to the main library so tests can exercise it.
target_sources(ojd_client PRIVATE src/sunshine_bridge.cpp)
