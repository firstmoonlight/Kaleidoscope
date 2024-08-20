#ifndef __PARSER_H__
#define __PARSER_H__
//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//
#include <map>

void MainLoop();
int getNextToken();
extern std::map<char, int> BinopPrecedence;

#endif