#include "expr.h"
#include "stack.h"
#include "builtin.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/ADT/APSInt.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>

#include <iostream>
#include <sstream>
#include <map>


typedef Stack<llvm::Value *> VarStack;
typedef StackWrapper<llvm::Value *> VarStackWrapper;

VarStack variables;
static llvm::IRBuilder<> builder(llvm::getGlobalContext());
static int errCnt;

#if 1
#define TRACE() std::cerr << __FILE__ << ":" << __LINE__ << "::" << __PRETTY_FUNCTION__ << std::endl
#else
#define TRACE()
#endif

llvm::Value *ErrorV(const std::string& msg)
{
    std::cerr << msg << std::endl;
    errCnt++;
    return 0;
}

static llvm::Function *ErrorF(const std::string& msg)
{
    ErrorV(msg);
    return 0;
}

llvm::Value* MakeConstant(int val, llvm::Type* ty)
{
    return llvm::ConstantInt::get(ty, val);
}

llvm::Value* MakeIntegerConstant(int val)
{
    return MakeConstant(val, Types::GetType(Types::Integer));
}

static llvm::Value* MakeBooleanConstant(int val)
{
    return MakeConstant(val, Types::GetType(Types::Boolean));
}

static llvm::Value* MakeCharConstant(int val)
{
    return MakeConstant(val, Types::GetType(Types::Char));
}

static llvm::AllocaInst* CreateAlloca(llvm::Function* fn, const VarDef& var)
{
    llvm::IRBuilder<> bld(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    llvm::Type* ty = Types::GetType(var.Type());
    if (!ty)
    {
	assert(0 && "Can't find type");
	return 0;
    }
    return bld.CreateAlloca(ty, 0, var.Name());
}

std::string ExprAST::ToString()
{
    std::stringstream ss;
    Dump(ss);
    return ss.str();
}

void RealExprAST::DoDump(std::ostream& out) const
{ 
    out << "Real: " << val;
}

llvm::Value* RealExprAST::CodeGen()
{
    TRACE();
    return llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(val));
}

void IntegerExprAST::DoDump(std::ostream& out) const
{ 
    out << "Integer: " << val;
}

llvm::Value* IntegerExprAST::CodeGen()
{
    TRACE();
    llvm::Value *v = MakeIntegerConstant(val);
    return v;
}

void CharExprAST::DoDump(std::ostream& out) const
{ 
    out << "Char: '" << val << "'";
}

llvm::Value* CharExprAST::CodeGen()
{
    TRACE();
    llvm::Value *v = MakeCharConstant(val);
    return v;
}


void VariableExprAST::DoDump(std::ostream& out) const
{ 
    out << "Variable: " << name;
}

llvm::Value* VariableExprAST::CodeGen()
{ 
    // Look this variable up in the function.
    TRACE();
    llvm::Value* v = Address();
    if (!v)
    {
	return 0;
    }
    return builder.CreateLoad(v, name.c_str()); 
}

llvm::Value* VariableExprAST::Address()
{
    TRACE();
    llvm::Value* v = variables.Find(name);
    if (!v)
    {
	return ErrorV(std::string("Unknown variable name '") + name + "'");
    }
    return v;
}

void ArrayExprAST::DoDump(std::ostream& out) const
{ 
    out << "Array: " << name;
    out << "[";
    bool first = true;
    for(auto i : indices)
    {
	if (!first)
	{
	    out << ", ";
	}
	first = false;
	i->Dump(out);
    }
}

llvm::Value* ArrayExprAST::Address()
{
    TRACE();
    llvm::Value* v = expr->Address();
    if (!v)
    {
	return ErrorV(std::string("Unknown variable name '") + name + "'");
    }
    llvm::Value* index; 
    for(size_t i = 0; i < indices.size(); i++)
    {
	TRACE();
	/* TODO: Add range checking? */
	index = indices[i]->CodeGen();
	if (!index)
	{
	    return ErrorV("Expression failed for index");
	}
	if (!index->getType()->isIntegerTy())
	{
	    return ErrorV("Index is supposed to be integral type");
	}
	llvm::Type* ty = index->getType();
	index = builder.CreateSub(index, MakeConstant(ranges[i]->GetStart(), ty));
	index = builder.CreateMul(index, MakeConstant(indexmul[i], ty));
    }
    std::vector<llvm::Value*> ind;
    ind.push_back(MakeIntegerConstant(0));
    ind.push_back(index);
    v = builder.CreateGEP(v, ind, "valueindex");
    return v;
}

void PointerExprAST::DoDump(std::ostream& out) const
{
    out << "Pointer:";
    pointer->Dump(out);
}


llvm::Value* PointerExprAST::CodeGen()
{ 
    // Look this variable up in the function.
    TRACE();
    llvm::Value* v = pointer->CodeGen();
    if (!v)
    {
	return 0;
    }
    if (!v->getType()->isPointerTy())
    {
	return ErrorV("Expected pointer type.");
    }
    return builder.CreateLoad(v, "ptr"); 
}

llvm::Value* PointerExprAST::Address()
{
    TRACE();
    VariableExprAST* vp = dynamic_cast<VariableExprAST*>(pointer);
    if (!vp)
    {
	return ErrorV("Taking address of non-variable type.");
    }
    llvm::Value* v = vp->CodeGen();
    if (!v)
    {
	return 0;
    }
    return v;
}

void BinaryExprAST::DoDump(std::ostream& out) const
{ 
    out << "BinaryOp: ";
    lhs->Dump(out);
    oper.Dump(out);
    rhs->Dump(out); 
}

llvm::Value* BinaryExprAST::CodeGen()
{
    TRACE();
    llvm::Value *l = lhs->CodeGen();
    llvm::Value *r = rhs->CodeGen();
    
    if (l == 0 || r == 0) 
    {
	return 0;
    }

    llvm::Type::TypeID rty = r->getType()->getTypeID();
    llvm::Type::TypeID lty = l->getType()->getTypeID();

    /* Convert right hand side to double if left is double, and right is integer */
    if (rty == llvm::Type::IntegerTyID  &&
	lty == llvm::Type::DoubleTyID)
    {
	r = builder.CreateSIToFP(r, Types::GetType(Types::Real), "tofp");
	r->dump();
	rty = r->getType()->getTypeID();
    }	

    if (rty != lty)
    {
	std::cout << "Different types..." << std::endl;
	l->dump();
	r->dump();
	assert(0 && "Different types...");
    }

    if (rty == llvm::Type::IntegerTyID)
    {
	switch(oper.GetType())
	{
	case Token::Plus:
	    return builder.CreateAdd(l, r, "addtmp");
	case Token::Minus:
	    return builder.CreateSub(l, r, "subtmp");
	case Token::Multiply:
	    return builder.CreateMul(l, r, "multmp");
	case Token::Divide:
	    return builder.CreateSDiv(l, r, "divtmp");

	case Token::Equal:
	    return builder.CreateICmpEQ(l, r, "eq");
	case Token::NotEqual:
	    return builder.CreateICmpNE(l, r, "ne");
	case Token::LessThan:
	    return builder.CreateICmpSLT(l, r, "lt");
	case Token::LessOrEqual:
	    return builder.CreateICmpSLE(l, r, "le");
	case Token::GreaterThan:
	    return builder.CreateICmpSGT(l, r, "gt");
	case Token::GreaterOrEqual:
	    return builder.CreateICmpSGE(l, r, "ge");
	    
	default:
	    return ErrorV(std::string("Unknown token: ") + oper.ToString());
	}
    }
    else if (rty == llvm::Type::DoubleTyID)
    {
	switch(oper.GetType())
	{
	case Token::Plus:
	    return builder.CreateFAdd(l, r, "addtmp");
	case Token::Minus:
	    return builder.CreateFSub(l, r, "subtmp");
	case Token::Multiply:
	    return builder.CreateFMul(l, r, "multmp");
	case Token::Divide:
	    return builder.CreateFDiv(l, r, "divtmp");

	case Token::Equal:
	    return builder.CreateFCmpOEQ(l, r, "eq");
	case Token::NotEqual:
	    return builder.CreateFCmpONE(l, r, "ne");
	case Token::LessThan:
	    return builder.CreateFCmpOLT(l, r, "lt");
	case Token::LessOrEqual:
	    return builder.CreateFCmpOLE(l, r, "le");
	case Token::GreaterThan:
	    return builder.CreateFCmpOGT(l, r, "gt");
	case Token::GreaterOrEqual:
	    return builder.CreateFCmpOGE(l, r, "ge");

	default:
	    return ErrorV(std::string("Unknown token: ") + oper.ToString());
	}
    }
    else
    {
	l->dump();
	oper.Dump(std::cout);
	r->dump();
	return ErrorV("Huh?");
    }
}

void UnaryExprAST::DoDump(std::ostream& out) const
{ 
    out << "Unary: " << oper.ToString();
    rhs->Dump(out);
}

llvm::Value* UnaryExprAST::CodeGen()
{
    llvm::Value* r = rhs->CodeGen();
    llvm::Type::TypeID rty = r->getType()->getTypeID();
    if (rty == llvm::Type::IntegerTyID)
    {
	switch(oper.GetType())
	{
	case Token::Minus:
	    return builder.CreateNeg(r, "minus");
	default:
	    return ErrorV(std::string("Unknown token: ") + oper.ToString());
	}
    }
    else if (rty == llvm::Type::DoubleTyID)
    {
	switch(oper.GetType())
	{
	case Token::Minus:
	    return builder.CreateFNeg(r, "minus");
	default:
	    return ErrorV(std::string("Unknown token: ") + oper.ToString());
	}
    }
    return ErrorV(std::string("Unknown type: ") + oper.ToString());
}

void CallExprAST::DoDump(std::ostream& out) const
{ 
    out << "call: " << callee << "(";
    for(auto i : args)
    {
	i->Dump(out);
    }
    out << ")";
}

llvm::Value* CallExprAST::CodeGen()
{
    TRACE();
    if (Builtin::IsBuiltin(callee))
    {
	return Builtin::CodeGen(builder, callee, args);
    }
    
    assert(proto && "Function prototype should be set in this case!");

    llvm::Function* calleF = theModule->getFunction(callee);
    if (!calleF)
    {
	return ErrorV(std::string("Unknown function ") + callee + " referenced");
    }
    if (calleF->arg_size() != args.size())
    {
	return ErrorV(std::string("Incorrect number of arguments for ") + callee + ".");
    }

    std::vector<llvm::Value*> argsV;
    const std::vector<VarDef>& vdef = proto->Args();
    std::vector<VarDef>::const_iterator viter = vdef.begin();
    assert(vdef.size() == args.size());
    for(auto i : args)
    {
	llvm::Value* v;
	if (viter->IsRef())
	{
	    VariableExprAST* vi = dynamic_cast<VariableExprAST*>(i);
	    if (!vi)
	    {
		return ErrorV("Args declared with 'var' must be a variable!");
	    }
	    v = vi->Address();
	}
	else
	{
	    v = i->CodeGen();
	}
	if (!v)
	{
	    return ErrorV("Invalid argument for " + callee + " (" + i->ToString() + ")");
	}
	
	argsV.push_back(v);
	viter++;
    }
    if (calleF->getReturnType()->getTypeID() == llvm::Type::VoidTyID) 
	return builder.CreateCall(calleF, argsV, "");
    else
	return builder.CreateCall(calleF, argsV, "calltmp");
}

void BlockAST::DoDump(std::ostream& out) const
{
    out << "Block: Begin " << std::endl;
    for(auto p = content; p; p = p->Next()) 
    {
	p->Dump(out);
    }
    out << "Block End;" << std::endl;
}

llvm::Value* BlockAST::CodeGen()
{
    TRACE();
    llvm::Value *v = 0;
    for(ExprAST *e = content; e; e = e->Next())
    {
	v = e->CodeGen();
	assert(v && "Expect codegen to work!");
    }
    return v;
}

void PrototypeAST::DoDump(std::ostream& out) const
{ 
    out << "Prototype: name: " << name << "(" << std::endl;
    for(auto i : args)
    {
	i.Dump(out); 
	out << std::endl;
    }
    out << ")";
}

llvm::Function* PrototypeAST::CodeGen()
{
    TRACE();
    std::vector<llvm::Type*> argTypes;
    for(auto i : args)
    {
	llvm::Type* ty = Types::GetType(i.Type());
	if (!ty)
	{
	    return ErrorF(std::string("Invalid type for argument") + i.Name() + "...");
	}
	if (i.IsRef())
	{
	    ty = llvm::PointerType::getUnqual(ty);
	}
	argTypes.push_back(ty);
    }
    llvm::Type* resTy = Types::GetType(resultType);
    llvm::FunctionType* ft = llvm::FunctionType::get(resTy, argTypes, false);
    llvm::Function* f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, theModule);

    // Validate function. 
    if (f->getName() != name)
    {
	f->eraseFromParent();
	f = theModule->getFunction(name);
	
	if (!f->empty())
	{
	    return ErrorF(std::string("redefinition of function: ") + name);
	}

	if (f->arg_size() != args.size())
	{
	    return ErrorF(std::string("Change in number of arguemts for function: ") + name);
	}	    
    }

    return f;
}

void PrototypeAST::CreateArgumentAlloca(llvm::Function* fn)
{
    unsigned idx = 0;
    for(llvm::Function::arg_iterator ai = fn->arg_begin(); 
	idx < args.size();
	idx++, ai++)
    {
	llvm::Value* a;
	if (args[idx].IsRef())
	{
	    a = ai; 
	}
	else
	{
	    a=CreateAlloca(fn, args[idx]);
	    builder.CreateStore(ai, a);
	}
	if (!variables.Add(args[idx].Name(), a))
	{
	    ErrorF(std::string("Duplicate variable name ") + args[idx].Name());
	}
    }
    if (resultType->Type() != Types::Void)
    {
	llvm::AllocaInst* a=CreateAlloca(fn, VarDef(name, resultType));
	variables.Add(name, a);
    }
}

void FunctionAST::DoDump(std::ostream& out) const
{ 
    out << "Function: " << std::endl;
    proto->Dump(out);
    out << "Function body:" << std::endl;
    body->Dump(out);
}

llvm::Function* FunctionAST::CodeGen()
{
    VarStackWrapper w(variables);
    TRACE();
    llvm::Function* theFunction = proto->CodeGen();
    if (!theFunction)
    {
	return 0;
    }
    if (proto->IsForward())
    {
	return theFunction;
    }

    llvm::BasicBlock *bb = llvm::BasicBlock::Create(llvm::getGlobalContext(), "entry", theFunction);
    builder.SetInsertPoint(bb);

    proto->CreateArgumentAlloca(theFunction);

    if (varDecls)
    {
	varDecls->SetFunction(theFunction);
	varDecls->CodeGen();
    }

    variables.Dump(std::cerr);
    llvm::Value *block = body->CodeGen();
    if (!block && !body->IsEmpty())
    {
	return 0;
    }

    if (proto->ResultType()->Type() == Types::Void)
    {
	builder.CreateRetVoid();
    }
    else
    {
	llvm::Value* v = variables.Find(proto->Name());
	assert(v);
	llvm::Value* retVal = builder.CreateLoad(v);
	builder.CreateRet(retVal);
    }

    TRACE();
    theFunction->dump();
    verifyFunction(*theFunction);
    
    fpm->run(*theFunction);
    return theFunction;
}

void StringExprAST::DoDump(std::ostream& out) const
{
    out << "String: '" << val << "'" << std::endl;
}

llvm::Value* StringExprAST::CodeGen()
{
    TRACE();
    return builder.CreateGlobalStringPtr(val.c_str(), "_string");
}

void AssignExprAST::DoDump(std::ostream& out) const
{
    out << "Assign: " << std::endl;
    lhs->Dump(out);
    out << ":=";
    rhs->Dump(out);
}

llvm::Value* AssignExprAST::CodeGen()
{
    TRACE();
    VariableExprAST* lhsv = dynamic_cast<VariableExprAST*>(lhs);
    if (!lhsv)
    {
	lhs->Dump(std::cerr);
	return ErrorV("Left hand side of assignment must be a variable");
    }
    
    llvm::Value* v = rhs->CodeGen();
    llvm::Value* dest = lhsv->Address();
    if (!dest)
    {
	return ErrorV(std::string("Unknown variable name ") + lhsv->Name());
    }
    llvm::Type::TypeID lty = dest->getType()->getContainedType(0)->getTypeID();
    llvm::Type::TypeID rty = v->getType()->getTypeID();

    if (rty == llvm::Type::IntegerTyID  &&
	lty == llvm::Type::DoubleTyID)
    {
	v = builder.CreateSIToFP(v, Types::GetType(Types::Real), "tofp");
	rty = v->getType()->getTypeID();
    }	
    assert(rty == lty && 
	   "Types must be the same in assignment.");
    
    builder.CreateStore(v, dest);
    
    return v;
}

void IfExprAST::DoDump(std::ostream& out) const
{
    out << "if: " << std::endl;
    cond->Dump(out);
    out << "then: ";
    then->Dump(out);
    out << " else::";
    other->Dump(out);
}

llvm::Value* IfExprAST::CodeGen()
{
    TRACE();
    llvm::Value *condv = cond->CodeGen();
    if (!condv)
    {
	return 0;
    }

    if (condv->getType()->getTypeID() ==  llvm::Type::IntegerTyID)
    {
	condv = builder.CreateICmpNE(condv, MakeBooleanConstant(0), "ifcond");
    }
    else
    {
	assert(0 && "Only integer expressions allowed in if-statement");
    }
    llvm::Function *theFunction = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "then", theFunction);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "else");
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "ifcont");

    builder.CreateCondBr(condv, thenBB, elseBB);

    builder.SetInsertPoint(thenBB);

    llvm::Value* thenV = then->CodeGen();
    if (!thenV)
    {
	return 0;
    }

    builder.CreateBr(mergeBB);
    thenBB = builder.GetInsertBlock();

    theFunction->getBasicBlockList().push_back(elseBB);
    builder.SetInsertPoint(elseBB);

    llvm::Value* elseV =  other->CodeGen();

    builder.CreateBr(mergeBB);
    elseBB = builder.GetInsertBlock();
    
    theFunction->getBasicBlockList().push_back(mergeBB);
    builder.SetInsertPoint(mergeBB);

    llvm::PHINode* pn = builder.CreatePHI(llvm::Type::getInt32Ty(llvm::getGlobalContext()), 2, "iftmp");

    pn->addIncoming(thenV, thenBB);
    pn->addIncoming(elseV, elseBB);

    return pn;
}

void ForExprAST::DoDump(std::ostream& out) const
{
    out << "for: " << std::endl;
    start->Dump(out);
    if (stepDown)
	out << " downto ";
    else
	out << " to ";
    end->Dump(out);
    out << " do ";
    body->Dump(out);
}

llvm::Value* ForExprAST::CodeGen()
{
    TRACE();

    llvm::Function* theFunction = builder.GetInsertBlock()->getParent();
    llvm::Value* var = variables.Find(varName);

    llvm::Value* startV = start->CodeGen();
    if (!startV)
    {
	return 0;
    }

    builder.CreateStore(startV, var); 

    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "loop", theFunction);    
    builder.CreateBr(loopBB);
    builder.SetInsertPoint(loopBB);

    if (!body->CodeGen())
    {
	return 0;
    }
    llvm::Value* stepVal = MakeConstant((stepDown)?-1:1, startV->getType());
    llvm::Value* curVar = builder.CreateLoad(var, varName.c_str());
    llvm::Value* nextVar = builder.CreateAdd(curVar, stepVal, "nextvar");

    builder.CreateStore(nextVar, var);
    
    llvm::Value* endCond;
    llvm::Value* endV = end->CodeGen();
    if (stepDown) 
    {
	endCond = builder.CreateICmpSGE(nextVar, endV, "loopcond");
    }
    else
    {
	endCond = builder.CreateICmpSLE(nextVar, endV, "loopcond");
    }

    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "afterloop", 
							 theFunction);
    
    builder.CreateCondBr(endCond, loopBB, afterBB);
    
    builder.SetInsertPoint(afterBB);

    return afterBB;
}

void WhileExprAST::DoDump(std::ostream& out) const
{
    out << "While: ";
    cond->Dump(out);
    out << " Do: ";
    body->Dump(out);
}

llvm::Value* WhileExprAST::CodeGen()
{
    TRACE();
    llvm::Function *theFunction = builder.GetInsertBlock()->getParent();

    /* We will need a "prebody" before the loop, a "body" and an "after" basic block  */
    llvm::BasicBlock* preBodyBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "prebody", 
							   theFunction);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "body", 
							 theFunction);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "after", 
							 theFunction);

    builder.CreateBr(preBodyBB);
    builder.SetInsertPoint(preBodyBB);
    
    llvm::Value* condv = cond->CodeGen();

    llvm::Value* endCond = builder.CreateICmpEQ(condv, MakeBooleanConstant(0), "whilecond");
    builder.CreateCondBr(endCond, afterBB, bodyBB); 

    builder.SetInsertPoint(bodyBB);
    if (!body->CodeGen())
    {
	return 0;
    }
    builder.CreateBr(preBodyBB);
    builder.SetInsertPoint(afterBB);
    
    return afterBB;
}

void RepeatExprAST::DoDump(std::ostream& out) const
{
    out << "Repeat: ";
    body->Dump(out);
    out << " until: ";
    cond->Dump(out);
}

llvm::Value* RepeatExprAST::CodeGen()
{
    llvm::Function *theFunction = builder.GetInsertBlock()->getParent();

    /* We will need a "body" and an "after" basic block  */
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "body", 
							 theFunction);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "after", 
							 theFunction);

    builder.CreateBr(bodyBB);
    builder.SetInsertPoint(bodyBB);
    if (!body->CodeGen())
    {
	return 0;
    }
    llvm::Value* condv = cond->CodeGen();
    llvm::Value* endCond = builder.CreateICmpNE(condv, MakeBooleanConstant(0), "untilcond");
    builder.CreateCondBr(endCond, afterBB, bodyBB); 

    builder.SetInsertPoint(afterBB);
    
    return afterBB;
}

void WriteAST::DoDump(std::ostream& out) const
{
    if (isWriteln)
    {
	out << "Writeln(";
    }
    else
    {
	out << "Write(";
    }
    bool first = true;
    for(auto a : args)
    {
	if (!first)
	{
	    out << ", ";
	}
	first = false;
	a.expr->Dump(out);
	if (a.width)
	{
	    out << ":";
	    a.width->Dump(out);
	}
	if (a.precision)
	{
	    out << ":";
	    a.precision->Dump(out);
	}
    }
    out << ")";
}

static llvm::Function *CreateWriteFunc(llvm::Type* ty)
{
    std::string suffix;
    std::vector<llvm::Type*> argTypes;
    llvm::Type* resTy = Types::GetType(Types::Void);
    if (ty)
    {
	if (ty == Types::GetType(Types::Char))
	{
	    argTypes.push_back(ty);
	    argTypes.push_back(Types::GetType(Types::Integer));
	    suffix = "char";
	}
	else if (ty->isIntegerTy())
	{
	    // Make args of two integers. 
	    argTypes.push_back(ty);
	    argTypes.push_back(ty);
	    suffix = "int";
	}
	else if (ty->isDoubleTy())
	{
	    // Args: double, int, int
	    argTypes.push_back(ty);
	    llvm::Type* t = Types::GetType(Types::Integer); 
	    argTypes.push_back(t);
	    argTypes.push_back(t);
	    suffix = "real";
	}
	else if (ty->isPointerTy())
	{
	    if (ty->getContainedType(0) != Types::GetType(Types::Char))
	    {
		return ErrorF("Invalid type argument for write");
	    }
	    argTypes.push_back(ty);
	    llvm::Type* t = Types::GetType(Types::Integer); 
	    argTypes.push_back(t);
	    suffix = "str";
	}
	else
	{
	    return 0;
	}
    }
    else
    {
	suffix = "nl";
    }
    std::string name = std::string("__write_") + suffix;
    llvm::FunctionType* ft = llvm::FunctionType::get(resTy, argTypes, false);
    llvm::Function* f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, 
					       name, theModule);
    if (f->getName() != name)
    {
	f->eraseFromParent();
	f = theModule->getFunction(name);
    }

    return f;
}

llvm::Value* WriteAST::CodeGen()
{
    for(auto arg: args)
    {
	std::vector<llvm::Value*> argsV;
	llvm::Value* v = arg.expr->CodeGen();
	if (!v)
	{
	    return ErrorV("Argument codegen failed");
	}
	argsV.push_back(v);
	llvm::Type *ty = v->getType();
	llvm::Function* f = CreateWriteFunc(ty);
	llvm::Value* w;
	if (!arg.width)
	{
	    if (ty == Types::GetType(Types::Integer))
	    {
		w = MakeIntegerConstant(13);
	    }
	    else if (ty->isDoubleTy())
	    {
		w = MakeIntegerConstant(15);
	    }
	    else
	    {
		w = MakeIntegerConstant(0);
	    }
	}   
	else
	{
	    w = arg.width->CodeGen();
	    assert(w && "Expect width expression to generate code ok");
	}

	if (!w->getType()->isIntegerTy())
	{
	    return ErrorV("Expected width to be integer value");
	}
	argsV.push_back(w);
	if (ty->isDoubleTy())
	{
	    llvm::Value* p;
	    if (arg.precision)
	    {
		p = arg.precision->CodeGen();
		if (!p->getType()->isIntegerTy())
		{
		    return ErrorV("Expected precision to be integer value");
		}
	    }
	    else
	    {
		p = MakeIntegerConstant(-1);
	    }
	    argsV.push_back(p);
	}
	builder.CreateCall(f, argsV, "");
    }
    if (isWriteln)
    {
	llvm::Function* f = CreateWriteFunc(0);
	builder.CreateCall(f, std::vector<llvm::Value*>(), "");
    }
    return MakeIntegerConstant(0);
}

void ReadAST::DoDump(std::ostream& out) const
{
    if (isReadln)
    {
	out << "Readln(";
    }
    else
    {
	out << "Read(";
    }
    bool first = true;
    for(auto a : args)
    {
	if (!first)
	{
	    out << ", ";
	}
	first = false;
	a->Dump(out);
    }
    out << ")";
}

static llvm::Function *CreateReadFunc(llvm::Type* ty)
{
    std::string suffix;
    std::vector<llvm::Type*> argTypes;
    llvm::Type* resTy = Types::GetType(Types::Void);
    if (ty)
    {
	if (!ty->isPointerTy())
	{
	    return ErrorF("Read argument is not pointer type!");
	}
	llvm::Type* innerTy = ty->getContainedType(0);
	if (innerTy->isIntegerTy())
	{
	    // Make args of two integers. 
	    argTypes.push_back(ty);
	    suffix = "int";
	}
	else if (innerTy->isDoubleTy())
	{
	    // Args: double, int, int
	    argTypes.push_back(ty);
	    suffix = "real";
	}
	else if (innerTy == Types::GetType(Types::Char))
	{
	    argTypes.push_back(ty);
	    suffix = "chr";
	}
	else
	{
	    return ErrorF("Invalid type argument for read");
	}
    }
    else
    {
	suffix = "nl";
    }
    std::string name = std::string("__read_") + suffix;
    llvm::FunctionType* ft = llvm::FunctionType::get(resTy, argTypes, false);
    llvm::Function* f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, 
					       name, theModule);
    if (f->getName() != name)
    {
	f->eraseFromParent();
	f = theModule->getFunction(name);
    }

    return f;
}

llvm::Value* ReadAST::CodeGen()
{
    for(auto arg: args)
    {
	std::vector<llvm::Value*> argsV;
	VariableExprAST* vexpr = dynamic_cast<VariableExprAST*>(arg);
	if (!vexpr)
	{
	    return ErrorV("Argument for read/readln should be a variable");
	}
	
	llvm::Value* v = vexpr->Address();
	if (!v)
	{
	    return 0;
	}
	argsV.push_back(v);
	llvm::Type *ty = v->getType();
	llvm::Function* f = CreateReadFunc(ty);

	builder.CreateCall(f, argsV, "");
    }
    if (isReadln)
    {
	llvm::Function* f = CreateReadFunc(0);
	builder.CreateCall(f, std::vector<llvm::Value*>(), "");
    }
    return MakeIntegerConstant(0);
}

void VarDeclAST::DoDump(std::ostream& out) const
{
    out << "Var ";
    for(auto v : vars)
    {
	v.Dump(out);
	out << std::endl;
    }
}

llvm::Value* VarDeclAST::CodeGen()
{
    TRACE();
    // Are we declaring global variables  - no function!
    llvm::Value *v = 0;
    for(auto var : vars)
    {
	if (!func)
	{
	    llvm::Type     *ty = Types::GetType(var.Type());
	    llvm::Constant *init = llvm::Constant::getNullValue(ty);
	    v = new llvm::GlobalVariable(*theModule, ty, false, 
					 llvm::Function::InternalLinkage, init, var.Name().c_str());
	}
	else
	{
	    v = CreateAlloca(func, var);
	}
	if (!variables.Add(var.Name(), v))
	{
	    return ErrorV(std::string("Duplicate name ") + var.Name() + "!");
	}
    }
    return v;
}

int GetErrors(void)
{
    return errCnt;
}
