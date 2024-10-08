#include "ast.h"
#include "logger.h"

// This is an object that owns LLVM core data structures
std::unique_ptr<llvm::LLVMContext> TheContext;
// This is a helper object that makes easy to generate LLVM instructions
std::unique_ptr<llvm::IRBuilder<>> Builder;
// This is an LLVM construct that contains functions and global variables
std::unique_ptr<llvm::Module> TheModule;
// This map keeps track of which values are defined in the current scope
std::map<std::string, llvm::AllocaInst *> NamedValues;

std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;

std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
llvm::ExitOnError ExitOnErr;

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
std::map<char, int> BinopPrecedence;

/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
static llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction,
                                          llvm::StringRef VarName) {
  llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(llvm::Type::getDoubleTy(*TheContext), nullptr, VarName);
}

llvm::raw_ostream &indent(llvm::raw_ostream &O, int size) {
  return O << std::string(size, ' ');
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//
llvm::Value *NumberExprAST::codegen() {
  KSDbgInfo.emitLocation(this);
  return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Val));
}

llvm::Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
  llvm::AllocaInst *A = NamedValues[Name];
  if (!A)
    LogErrorV("Unknown variable name");

  KSDbgInfo.emitLocation(this);
  // Load the value
  return Builder->CreateLoad(A->getAllocatedType(), A, Name.c_str());
}

llvm::Value *VarExprAST::codegen() {
  std::vector<llvm::AllocaInst *> OldBindings;

  llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Register all variables and emit their initializer.
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
    const std::string &VarName = VarNames[i].first;
    ExprAST *Init = VarNames[i].second.get();

    // Emit the initializer before adding the variable to scope, this prevents
    // the initializer from referencing the variable itself, and permits stuff
    // like this:
    //  var a = 1 in
    //    var a = a in ...   # refers to outer 'a'.
    llvm::Value *InitVal;
    if (Init) {
      InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;
    } else { // If not specified, use 0.0.
      InitVal = llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0));
    }

    llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);

    // Remember the old variable binding so that we can restore the binding when
    // we unrecurse.
    OldBindings.push_back(NamedValues[VarName]);

    // Remember this binding.
    NamedValues[VarName] = Alloca;
  }

  KSDbgInfo.emitLocation(this);
  // Codegen the body, now that all vars are in scope.
  llvm::Value *BodyVal = Body->codegen();
  if (!BodyVal)
    return nullptr;

  // Pop all our variables from scope.
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i)
    NamedValues[VarNames[i].first] = OldBindings[i];

  // Return the body computation.
  return BodyVal;
}

llvm::Function *getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // If no existing prototype exists, return null.
  return nullptr;
}

llvm::Value *UnaryExprAST::codegen() {
  llvm::Value *OperandV = Operand->codegen();
  if (!OperandV)
    return nullptr;

  llvm::Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  KSDbgInfo.emitLocation(this);
  return Builder->CreateCall(F, OperandV, "unop");
}

llvm::Value *BinaryExprAST::codegen() {
    KSDbgInfo.emitLocation(this);

  // Special case '=' because we don't want to emit the LHS as an expression.
  if (Op == '=') {
    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    VariableExprAST *LHSE = static_cast<VariableExprAST *>(LHS.get());
    if (!LHSE)
      return LogErrorV("destination of '=' must be a variable");
    // Codegen the RHS.
    llvm::Value *Val = RHS->codegen();
    if (!Val)
      return nullptr;

    // Look up the name.
    llvm::Value *Variable = NamedValues[LHSE->getName()];
    if (!Variable)
      return LogErrorV("Unknown variable name");

    Builder->CreateStore(Val, Variable);
    return Val;
  }
  llvm::Value *L = LHS->codegen();
  llvm::Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext),
                                 "booltmp");
  default:
    break;
  }

  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  llvm::Function* F = getFunction(std::string("binary") + Op);
  assert(F && "binary operator not found");

  llvm::Value *Ops[2] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}

llvm::Value *CallExprAST::codegen() {
    KSDbgInfo.emitLocation(this);

  // Look up the name in the global module table.
  llvm::Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  // If argument mismatch error.
  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<llvm::Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

llvm::Function *PrototypeAST::codegen() {
  // Make the function type:  double(double,double) etc.
  std::vector<llvm::Type *> Doubles(Args.size(), llvm::Type::getDoubleTy(*TheContext));
  llvm::FunctionType *FT =
      llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext), Doubles, false);

  llvm::Function *F =
      llvm::Function::Create(FT, llvm::Function::ExternalLinkage, Name, TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}

llvm::Function *FunctionAST::codegen() {
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto& P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  llvm::Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  // If this is an operator, install it.
  if (P.isBinaryOp())
    BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

  // Create a new basic block to start insertion into.
  llvm::BasicBlock *BB = llvm::BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Create a subprogram DIE for this function.
  llvm::DIFile *Unit = DBuilder->createFile(KSDbgInfo.TheCU->getFilename(),
                                      KSDbgInfo.TheCU->getDirectory());
  llvm::DIScope *FContext = Unit;
  unsigned LineNo = P.getLine();
  unsigned ScopeLine = LineNo;
  llvm::DISubprogram *SP = DBuilder->createFunction(
      FContext, P.getName(), llvm::StringRef(), Unit, LineNo,
      CreateFunctionType(TheFunction->arg_size()), ScopeLine,
      llvm::DINode::FlagPrototyped, llvm::DISubprogram::SPFlagDefinition);
  TheFunction->setSubprogram(SP);

  // Push the current scope.
  KSDbgInfo.LexicalBlocks.push_back(SP);

  // Unset the location for the prologue emission (leading instructions with no
  // location in a function are considered part of the prologue and the debugger
  // will run past them when breaking on a function)
  KSDbgInfo.emitLocation(nullptr);

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  unsigned ArgIdx = 0;
  for (auto &Arg : TheFunction->args()) {
    // Create an alloca for this variable.
    llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

    // Create a debug descriptor for the variable.
    llvm::DILocalVariable *D = DBuilder->createParameterVariable(
        SP, Arg.getName(), ++ArgIdx, Unit, LineNo, KSDbgInfo.getDoubleTy(),
        true);

    DBuilder->insertDeclare(Alloca, D, DBuilder->createExpression(),
                            llvm::DILocation::get(SP->getContext(), LineNo, 0, SP),
                            Builder->GetInsertBlock());

    // Store the initial value into the alloca.
    Builder->CreateStore(&Arg, Alloca);

    // Add arguments to variable symbol table.
    NamedValues[std::string(Arg.getName())] = Alloca;
  }

  KSDbgInfo.emitLocation(Body.get());
  if (llvm::Value *RetVal = Body->codegen()) {
    // Finish off the function.
    Builder->CreateRet(RetVal);

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    // Pop off the lexical block for the function.
    KSDbgInfo.LexicalBlocks.pop_back();

    // Optimize the function.
    // TheFPM->run(*TheFunction, *TheFAM);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();
   if (P.isBinaryOp())
    BinopPrecedence.erase(P.getOperatorName());
  // Pop off the lexical block for the function since we added it
  // unconditionally.
  KSDbgInfo.LexicalBlocks.pop_back();
  return nullptr;
}

llvm::Value* IfExprAST::codegen() {
  KSDbgInfo.emitLocation(this);

  llvm::Value* CondV = Cond->codegen();
  if (!CondV)
      return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  CondV = Builder->CreateFCmpONE(CondV, llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)), "ifcond");

  llvm::Function* TheFunction = Builder->GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block at the
  // end of the function.
  llvm::BasicBlock* ThenBB = llvm::BasicBlock::Create(*TheContext, "then", TheFunction);
  llvm::BasicBlock* ElseBB = llvm::BasicBlock::Create(*TheContext, "else");
  llvm::BasicBlock* MergeBB = llvm::BasicBlock::Create(*TheContext, "ifcont");

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then value
  Builder->SetInsertPoint(ThenBB);

  llvm::Value* ThenV = Then->codegen();
  if (!ThenV)
      return nullptr;
  
  Builder->CreateBr(MergeBB);
  // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
  ThenBB = Builder->GetInsertBlock();

  // Emit else block
  TheFunction->insert(TheFunction->end(), ElseBB);
  Builder->SetInsertPoint(ElseBB);

  llvm::Value* ElseV = Else->codegen();
  if (!ElseV) 
    return nullptr;
  
  Builder->CreateBr(MergeBB);
  // codegen of 'Else' can change the current block, update ElseBB for the PHI.
  ElseBB = Builder->GetInsertBlock();

  // Emit merge block
  TheFunction->insert(TheFunction->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  llvm::PHINode* PN = Builder->CreatePHI(llvm::Type::getDoubleTy(*TheContext), 2, "iftmp");

  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
}

// Output for-loop as:
//   ...
//   start = startexpr
//   goto loop
// loop:
//   variable = phi [start, loopheader], [nextvariable, loopend]
//   ...
//   bodyexpr
//   ...
// loopend:
//   step = stepexpr
//   nextvariable = variable + step
//   endcond = endexpr
//   br endcond, loop, endloop
// outloop:
llvm::Value* ForExprAST::codegen() {
  llvm::Function* TheFunction = Builder->GetInsertBlock()->getParent();

  // Create an alloca for the variable in the entry block.
  llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
  KSDbgInfo.emitLocation(this);

  // Emit the start code first, without 'variable' in scope.
  llvm::Value* StartVal = Start->codegen();
  if (!StartVal)
      return nullptr;

  // Store the value into the alloca.
  Builder->CreateStore(StartVal, Alloca);

  // Make the new basic block for the loop header, inserting after current
  // block.
  llvm::BasicBlock* LoopBB = llvm::BasicBlock::Create(*TheContext, "loop", TheFunction);

  // Insert an explicit fall through from the current block to the LoopBB.
  Builder->CreateBr(LoopBB);

  // Start insertion in LoopBB.
  Builder->SetInsertPoint(LoopBB);

  // Within the loop, the variable is defined equal to the PHI node.  If it
  // shadows an existing variable, we have to restore it, so save it now.
  llvm::AllocaInst *OldVal = NamedValues[VarName];
  NamedValues[VarName] = Alloca;
  
  // Emit the body of the loop.  This, like any other expr, can change the
  // current BB.  Note that we ignore the value computed by the body, but don't
  // allow an error.
  if (!Body->codegen())
    return nullptr;

  // Emit the step value 
  llvm::Value* StepVal = nullptr;
  if (Step) {
    StepVal = Step->codegen();
    if (!StepVal)
        return nullptr;
  } else {
    // if not specified, use 1.0
    StepVal = llvm::ConstantFP::get(*TheContext, llvm::APFloat(1.0));
  }

  // Compute the end condition
  llvm::Value* EndCond = End->codegen();
  if (!EndCond) 
    return nullptr;

 // Reload, increment, and restore the alloca.  This handles the case where
  // the body of the loop mutates the variable.
  llvm::Value *CurVar =
      Builder->CreateLoad(Alloca->getAllocatedType(), Alloca, VarName.c_str());
  llvm::Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
  Builder->CreateStore(NextVar, Alloca);

  // Convert condition to a bool by comparing non-equal to 0.0.
  EndCond = Builder->CreateFCmpONE( EndCond, llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)), "loopcond");

  // Create the "after loop" block and insert it.
  llvm::BasicBlock *AfterBB =
      llvm::BasicBlock::Create(*TheContext, "afterloop", TheFunction);
  
  // Insert the conditional branch into the end of LoopEndBB.
  Builder->CreateCondBr(EndCond, LoopBB, AfterBB);
  
  // Any new code will be inserted in AfterBB.
  Builder->SetInsertPoint(AfterBB);

  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for expr always returns 0.0.
  return llvm::Constant::getNullValue(llvm::Type::getDoubleTy(*TheContext));
}