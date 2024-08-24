add_library(Parser parser.cpp ast.cpp)
target_include_directories(Parser PUBLIC ${CMAKE_SOURCE_DIR} ${LLVM_INCLUDE_DIRS})
message("LLVM_INCLUDE_DIRS : " ${LLVM_INCLUDE_DIRS})
message("CMAKE_SOURCE_DIR : " ${CMAKE_SOURCE_DIR})