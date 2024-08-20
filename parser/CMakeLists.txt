add_library(Parser parser.cpp ast.cpp)
target_include_directories(Parser PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/llvm/include)
message("CMAKE_SOURCE_DIR : " ${CMAKE_SOURCE_DIR})