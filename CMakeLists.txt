cmake_minimum_required(VERSION 3.2)

PROJECT(main)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set (PATH_TO_LLVM ${CMAKE_SOURCE_DIR}/llvm)
set (search_paths
  ${PATH_TO_LLVM}
  ${PATH_TO_LLVM}/lib/cmake
  ${PATH_TO_LLVM}/lib/cmake/llvm
  ${PATH_TO_LLVM}/lib/cmake/clang
  ${PATH_TO_LLVM}/share/clang/cmake/
  ${PATH_TO_LLVM}/share/llvm/cmake/
)

find_package(LLVM REQUIRED CONFIG
             PATHS ${search_paths}
             NO_DEFAULT_PATH)
find_package(Clang REQUIRED CONFIG
             PATHS ${search_paths}
             NO_DEFAULT_PATH)

# message("LLVM_LIBS : " ${LLVM_LIBS})
message("LLVM_LIBRARY_DIRS : " ${LLVM_LIBRARY_DIRS})

add_subdirectory(lexer)
add_subdirectory(parser)
list(APPEND EXTRA_LIBS Parser Lexer)

add_executable(main main.cpp)

link_directories(${LLVM_LIBRARY_DIRS})
target_link_libraries(main PUBLIC ${EXTRA_LIBS} LLVMSupport LLVMCore)

target_include_directories(main PUBLIC "${PROJECT_BINARY_DIR}")
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)
install(TARGETS main DESTINATION bin)