cmake_minimum_required(VERSION 3.2)

PROJECT(main)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory(lexer)
add_subdirectory(parser)
list(APPEND EXTRA_LIBS Parser Lexer)

add_executable(main main.cpp)

target_link_libraries(main PUBLIC ${EXTRA_LIBS})

target_include_directories(main PUBLIC "${PROJECT_BINARY_DIR}")
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)
install(TARGETS main DESTINATION bin)