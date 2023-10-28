#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Argument.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMController.h"

// listener and visitor
#include "LucidusParserVisitor.h"
#include "LucidusParserBaseVisitor.h"

#include "MyVisitor.h"

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if(from.empty())
        return;
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

llvm::Value *get_size(llvm::Type *t, llvm::IRBuilder<>& irb)
{
    llvm::Value *sizePtr = irb.CreateGEP(t->getContainedType(0),
            irb.CreateIntToPtr(llvm::ConstantInt::get(irb.getInt8Ty(), 0), t),
            llvm::ConstantInt::get(irb.getInt8Ty(), 1));

    return irb.CreatePtrToInt(sizePtr, irb.getInt64Ty());
}

antlrcpp::Any MyVisitor::visitDec(LucidusParser::DecContext *ctx) {
        std::string functionName = ctx->ID()->getText();
        llvm::Type* rtype = getTypes(ctx->type(), this->controller, this->structs);
        std::vector<llvm::Type*> types;
        bool ellip = false;
        // std::cout << ctx->param().size() << std::endl;
        this->functionNameScope.insert({functionName, {std::vector<std::string>(), false}});
        if(ctx->param().size() !=0) {
            llvm::Type* type;
            LucidusParser::IdecContext * idec;
            for(int i = 0; i<ctx->param().size(); i++) {
                if(ctx->param(i)->DOTS() != nullptr) {
                    ellip = true;
                    this->functionNameScope[functionName].second = true;
                } else {
                    idec = ctx->param(i)->idec();
                    type = getTypes(idec->type(), this->controller, this->structs);
                    types.push_back(type);
                    this->functionNameScope[functionName].first.push_back(idec->ID()->getText());
                }
            }
        }
        controller->declareFunction(functionName.c_str(), llvm::FunctionType::get(rtype, types, ellip));
        return visitChildren(ctx);
}

antlrcpp::Any MyVisitor::visitExpr(LucidusParser::ExprContext *ctx) {
    if(ctx->INT() != nullptr && ctx->children.size() == 1) {
        int v = std::stoi(ctx->INT()->getText());
        llvm::Value *val = llvm::ConstantInt::get(llvm::Type::getInt32Ty(controller->ctx), v);
        return val;
    }else if(ctx->STRING() != nullptr && ctx->children.size() == 1) {
        // auto str = controller->builder->CreateGlobalStringPtr(ctx->STRING()->getText());
        // // load and return str
        // auto loadedStr = controller->builder->CreateLoad(str->getType(), str);
        // // now make loadStr llvm::Value* and return
        // return (llvm::Value*)loadedStr;
        // forget all that, make it an an array of chars, but in pointer form
        // ^:skull: now that I have done this I am too lazy to do it correctly.
        std::string str = ctx->STRING()->getText();
        replaceAll(str, "\\n", "\n");
        replaceAll(str, "\\t", "\t");
        replaceAll(str, "\\r", "\r");
        replaceAll(str, "\\v", "\v");
        replaceAll(str, "\\b", "\b");
        replaceAll(str, "\\f", "\f");
        replaceAll(str, "\\a", "\a");
        replaceAll(str, "\\e", "\e");
        replaceAll(str, "\\0", "\0");
        replaceAll(str, "\\'", "\'");
        replaceAll(str, "\\\"", "\"");
        replaceAll(str, "\\\\", "\\");
        replaceAll(str, "\\?", "\?");
        std::vector<llvm::Constant*> chars;
        for(int i = 1; i<str.length()-1; i++) {
            chars.push_back(llvm::ConstantInt::get(llvm::Type::getInt8Ty(controller->ctx), str[i]));
        }
        chars.push_back(llvm::ConstantInt::get(llvm::Type::getInt8Ty(controller->ctx), 0));
        auto arr = llvm::ConstantArray::get(llvm::ArrayType::get(llvm::Type::getInt8Ty(controller->ctx), chars.size()), chars);
        auto global = new llvm::GlobalVariable(*controller->module, llvm::ArrayType::get(llvm::Type::getInt8Ty(controller->ctx), chars.size()), true, llvm::GlobalValue::PrivateLinkage, arr, "str");
        auto ptr = controller->builder->CreateInBoundsGEP(global->getValueType(), global, {controller->builder->getInt32(0), controller->builder->getInt32(0)});
        return ptr;
    }else if(ctx->func() != nullptr) {
        return visit(ctx->func());
    }else if(ctx->ID() != nullptr && ctx->children.size() == 1) {
        // if variable is in function scope
        if(this->functionScope.count(ctx->ID()->getText()))
            return (llvm::Value*) controller->builder->CreateLoad(((llvm::AllocaInst*)this->functionScope[ctx->ID()->getText()])->getAllocatedType(),this->functionScope[ctx->ID()->getText()]);
        // else check if it is in global vars, if so, this->controller->module->getNamedGlobal(name)
        else if(this->controller->module->getNamedGlobal(ctx->ID()->getText()))
            return (llvm::Value*) controller->builder->CreateLoad(this->controller->module->getNamedGlobal(ctx->ID()->getText())->getValueType(), this->controller->module->getNamedGlobal(ctx->ID()->getText()));
        else {
            std::cout << "Variable " << ctx->ID()->getText() << " not found" << std::endl;
            return (llvm::Value*) llvm::ConstantPointerNull::get(llvm::Type::getInt32PtrTy(controller->ctx));
        }
    }else if(ctx->PLUS() != nullptr) {
        return (llvm::Value*) controller->builder->CreateAdd(std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->expr(0))), std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->expr(1))));
    }else if(ctx->PTR() != nullptr) {
        // make ref
        auto val = (llvm::Value*) std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->expr(0)));
        auto valptrptr = controller->builder->CreateAlloca(val->getType(), nullptr);
        controller->assignVariable((llvm::AllocaInst*)valptrptr, val);
        return (llvm::Value*)valptrptr;
        // return (llvm::Value*)llvm::ConstantPointerNull::get(llvm::Type::getInt32PtrTy(controller->ctx));
    } else if (ctx->STAR(0) && ctx->children.size() == ctx->STAR().size() + 1) {
        // make ref
        auto val = std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->expr(0)));
        // get dereferenced type
        auto dtype = val->getType(); // doesnt work
        dtype = dtype->getContainedType(0);
        auto valptr = controller->builder->CreateLoad(dtype, val);
        auto valptrptr = controller->builder->CreateAlloca(dtype, nullptr);
        controller->assignVariable((llvm::AllocaInst*)valptrptr, valptr);
        return (llvm::Value*)controller->getVariable(valptrptr);
    } else if(ctx->DOT() != nullptr && ctx->children.size() == 3) {
        auto structPtr = std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->expr(0)));
        auto structType = structPtr->getType()->getContainedType(0);
        auto structName = structType->getStructName();
        auto structMember = ctx->ID()->getText();
        // auto structMemberIndex = this->structs[structName]->getStructMemberIndex(structMember); // doesnt work, function is not defined
        auto structMemberIndex = 0;
        for(int i = 0; i<this->structs[structName]->getNumElements(); i++) {
            if(this->structNames[structName][i]==structMember) {
                structMemberIndex = i;
                break;
            }
        }
        auto structMemberType = this->structs[structName]->getStructElementType(structMemberIndex);
        auto structMemberPtr = controller->builder->CreateStructGEP(structType, structPtr, structMemberIndex);
        return (llvm::Value*)structMemberPtr;
        // return visitChildren(ctx);
    }else {
        // return (llvm::Value *)llvm::ConstantInt::get(llvm::Type::getInt32Ty(controller->ctx), 0);
        // return llvm nullptr LOOOL
        return (llvm::Value*) llvm::ConstantPointerNull::get(llvm::Type::getInt32PtrTy(controller->ctx));
    }

    // return visitChildren(ctx);
    // return  controller->builder->CreateCall(controller->module->getFunction("printf"), {});
}

antlrcpp::Any MyVisitor::visitDef(LucidusParser::DefContext *ctx) {
    this->controller->defineFunction(ctx->ID(0)->getText());
    this->functionScope = {};
    for(int i = 0; i<ctx->param().size(); i++) {
            if(ctx->param(i)->DOTS() == nullptr){
                this->functionScope[ctx->param(i)->idec()->ID()->getText()] = controller->declareVariable(ctx->param(i)->idec()->ID()->getText(),getTypes(ctx->param(i)->idec()->type(), controller, this->structs));
                // llvm::StoreInst* val = controller->assignVariable((llvm::AllocaInst*)this->functionScope[ctx->param(i)->idec()->ID()->getText()], this->functionParamScope[ctx->ID(i)->getText()][ctx->param(i)->idec()->ID()->getText()]);
                // ith argument value (from llvm):
                llvm::Value* arg = this->controller->module->getFunction(ctx->ID(0)->getText())->getArg(i);
                llvm::StoreInst* val = controller->assignVariable((llvm::AllocaInst*)this->functionScope[ctx->param(i)->idec()->ID()->getText()], arg);
            }
        }
    return visitChildren(ctx);
}

antlrcpp::Any MyVisitor::visitFunc(LucidusParser::FuncContext *ctx)  {
        auto func = this->controller->module->getFunction(ctx->ID()->getText());
        // get params
        std::vector<llvm::Value*> params;
        for(int i = 0; i<ctx->expr().size(); i++) {
            params.push_back(std::any_cast<llvm::Value*>((std::any)visit(ctx->expr(i))));
            // param name
        }
        return (llvm::Value*)this->controller->builder->CreateCall(func, params);
}

antlrcpp::Any MyVisitor::visitStat(LucidusParser::StatContext *ctx) {
    // std::cout << ctx->getRuleIndex() << std::endl;
    // std::cout << ctx->getText() << std::endl;
    if(ctx->expr() !=nullptr && ctx->children.size() == 1) {
        return visit(ctx->expr());
    } else if(ctx->ret() != nullptr && ctx->children.size() == 1) {
        return controller->builder->CreateRet(std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->ret()->expr())));
    }else if(ctx->vdec() != nullptr && ctx->children.size() == 1) {
        std::string name = ctx->vdec()->idec()->ID()->getText();
        llvm::Type* type = getTypes(ctx->vdec()->idec()->type(), this->controller, this->structs);
        // llvm::Value* val = controller->builder->CreateAlloca(type, std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->vdec()->expr())), name);
        llvm::AllocaInst* ptr = controller->declareVariable(name, type);
        llvm::StoreInst* val = controller->assignVariable(ptr, std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->vdec()->expr())));
        this->functionScope[name] = (llvm::Value*)ptr;
        return ptr;
    }else if(ctx->vdef() != nullptr && ctx->children.size() == 1) {
        std::string name = ctx->vdef()->ID()->getText();
        llvm::Value* val = std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->vdef()->expr()));
        
        controller->assignVariable((llvm::AllocaInst*)this->functionScope[name], val);
        return this->functionScope[name];
    }else if(ctx->edec() != nullptr && ctx->children.size() == 1) {
        std::string name = ctx->edec()->idec()->ID()->getText();
        llvm::Type* type = getTypes(ctx->edec()->idec()->type(), this->controller, this->structs);
        // llvm::Value* val = controller->builder->CreateAlloca(type, std::any_cast<llvm::Value*>((std::any)visitExpr(ctx->vdec()->expr())), name);
        llvm::AllocaInst* ptr = controller->declareVariable(name, type);
        this->functionScope[name] = (llvm::Value*)ptr;
        // return ptr;
        return visitChildren(ctx);
    }else
    return visitChildren(ctx);
}

std::any MyVisitor::visitStruct(LucidusParser::StructContext *ctx) {
    std::string name = ctx->ID()->getText();
    std::vector<llvm::Type*> types;
    for(int i = 0; i<ctx->idec().size(); i++) {
        types.push_back(getTypes(ctx->idec(i)->type(), this->controller, this->structs));
    }
    llvm::StructType* stype = llvm::StructType::create(this->controller->ctx, types, name);
    this->structs[name] = stype;
    stype->setName(name);
    // put struct into ir
    this->controller->module->getOrInsertGlobal(name, stype); // why does this segfault..
    // now for the names or whatever they are (ID inside idec)
    std::vector<std::string> names;
    for(int i = 0; i<ctx->idec().size(); i++) {
        names.push_back(ctx->idec(i)->ID()->getText());
    }
    this->structNames[name] = names;
    return stype;
}