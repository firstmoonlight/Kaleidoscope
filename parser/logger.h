#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <memory>
#include "llvm/IR/Value.h"

class ExprAST;
class PrototypeAST;

std::unique_ptr<ExprAST> LogError(const char *Str);
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str);
llvm::Value *LogErrorV(const char *Str);
#endif