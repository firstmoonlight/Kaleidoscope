//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "parser/ast.h"

int main() {
  // Install standard binary operators.
  // 1 is lowest precedence.
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40; // highest.

  // Prime the first token.
  fprintf(stderr, "ready> ");
  getNextToken();

  // Make the module, which holds all the code.
  TheModule = std::make_unique<llvm::Module>("my cool jit", TheContext);

  // Run the main "interpreter loop" now.
  MainLoop();

  // Print out all of the generated code.
  TheModule->print(llvm::errs(), nullptr);

  return 0;
}