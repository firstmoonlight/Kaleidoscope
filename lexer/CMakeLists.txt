add_library(Lexer lexer.cpp dbg.cpp)
target_include_directories(Lexer PUBLIC ${CMAKE_SOURCE_DIR} ${LLVM_INCLUDE_DIRS})