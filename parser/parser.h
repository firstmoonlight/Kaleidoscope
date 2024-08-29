#ifndef __PARSER_H__
#define __PARSER_H__
//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//
#include <map>
#include <memory>

class FunctionAST;
class PrototypeAST;

void MainLoop();
int getNextToken();
extern std::map<char, int> BinopPrecedence;
std::unique_ptr<FunctionAST> ParseDefinition();
std::unique_ptr<PrototypeAST> ParseExtern();
std::unique_ptr<FunctionAST> ParseTopLevelExpr();
void InitializeModuleAndManagers();
#endif