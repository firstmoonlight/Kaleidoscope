#ifndef __DBG_H__
#define __DBG_H__

#include "llvm/IR/DIBuilder.h"

class ExprAST;

extern std::unique_ptr<llvm::DIBuilder> DBuilder;

struct DebugInfo {
  llvm::DICompileUnit *TheCU;
  llvm::DIType *DblTy;
  std::vector<llvm::DIScope *> LexicalBlocks;

  void emitLocation(ExprAST *AST);
  llvm::DIType *getDoubleTy();
} ;

struct SourceLocation {
  int Line;
  int Col;
};

extern struct DebugInfo KSDbgInfo;
extern SourceLocation CurLoc;
extern SourceLocation LexLoc;

llvm::DISubroutineType *CreateFunctionType(unsigned NumArgs);
#endif