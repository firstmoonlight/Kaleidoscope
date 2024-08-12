add_library(Parser parser.cpp)
target_include_directories(Parser PUBLIC ${CMAKE_SOURCE_DIR})
message("CMAKE_SOURCE_DIR : " ${CMAKE_SOURCE_DIR})