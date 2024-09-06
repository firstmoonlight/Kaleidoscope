#include "llvm/IR/IRBuilder.h"
#include "parser/ast.h"

extern std::unique_ptr<llvm::IRBuilder<>> Builder;

//===----------------------------------------------------------------------===//
// Debug Info Support
//===----------------------------------------------------------------------===//
std::unique_ptr<llvm::DIBuilder> DBuilder;
struct DebugInfo KSDbgInfo;
SourceLocation CurLoc;
SourceLocation LexLoc = {1, 0};

llvm::DIType *DebugInfo::getDoubleTy() {
  if (DblTy)
    return DblTy;

  DblTy = DBuilder->createBasicType("double", 64, llvm::dwarf::DW_ATE_float);
  return DblTy;
}

void DebugInfo::emitLocation(ExprAST *AST) {
  if (!AST)
    return Builder->SetCurrentDebugLocation(llvm::DebugLoc());
  llvm::DIScope *Scope;
  if (LexicalBlocks.empty())
    Scope = TheCU;
  else
    Scope = LexicalBlocks.back();
  Builder->SetCurrentDebugLocation(llvm::DILocation::get(
      Scope->getContext(), AST->getLine(), AST->getCol(), Scope));
}

llvm::DISubroutineType *CreateFunctionType(unsigned NumArgs) {
  llvm::SmallVector<llvm::Metadata *, 8> EltTys;
  llvm::DIType *DblTy = KSDbgInfo.getDoubleTy();

  // Add the result type.
  EltTys.push_back(DblTy);

  for (unsigned i = 0, e = NumArgs; i != e; ++i)
    EltTys.push_back(DblTy);

  return DBuilder->createSubroutineType(DBuilder->getOrCreateTypeArray(EltTys));
}
