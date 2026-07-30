#pragma once

#include "lona/ast/astnode.hh"
#include "lona/module/compilation_unit.hh"
#include "lona/module/module_interface.hh"
#include "lona/sym/func.hh"
#include "lona/type/scope.hh"
#include "parser.hh"
#include <llvm-18/llvm/ADT/StringMap.h>
#include <llvm-18/llvm/ADT/StringSet.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lona {

namespace declarationsupport_impl {

enum class TopLevelDeclKind {
    StructType,
    Trait,
    Function,
    Global,
    Inline,
};

void
recordTopLevelDeclName(
    std::unordered_map<std::string, std::pair<TopLevelDeclKind, location>>
        &seen,
    const std::string &name, TopLevelDeclKind kind, const location &loc);

std::vector<string>
extractParamNames(AstFuncDecl *node, std::size_t skipLeadingArgs = 0);

std::vector<BindingKind>
extractParamBindingKinds(AstFuncDecl *node, std::size_t skipLeadingArgs = 0,
                         bool prependImplicitSelf = false);

std::string
resolveStructMethodOwnerTypeName(StructType *methodParent);

std::string
resolveStructMethodSymbolName(StructType *methodParent,
                              llvm::StringRef methodName,
                              ReceiverMode receiverMode);

std::string
resolveTraitMethodSymbolName(StructType *methodParent,
                             llvm::StringRef traitName,
                             llvm::StringRef methodName,
                             ReceiverMode receiverMode);

std::string
resolveExtensionMethodSymbolName(const CompilationUnit *unit,
                                 const std::string &targetTypeSpelling,
                                 llvm::StringRef methodName,
                                 ReceiverMode receiverMode,
                                 bool exportNamespace);

TypeClass *
extensionReceiverType(TypeTable *typeMgr, TypeClass *targetType,
                      ReceiverMode receiverMode);

TypeClass *
interfaceExtensionReceiverType(ModuleInterface *interface,
                               TypeClass *targetType,
                               ReceiverMode receiverMode);

TypeClass *
methodReceiverType(TypeTable *typeMgr, StructType *methodParent,
                   ReceiverMode receiverMode);

TypeClass *
interfaceMethodReceiverType(ModuleInterface *interface,
                            StructType *methodParent,
                            ReceiverMode receiverMode);

TypeClass *
resolveContextualSelfType(TypeTable *typeMgr, const CompilationUnit *unit,
                          TypeNode *node, TypeClass *selfType);

void
validateExtensionTargetShape(AstExtendDecl *node);

void
validateFunctionReceiverAccess(AstFuncDecl *node, StructType *methodParent);

void
rejectOpaqueStructByValue(TypeClass *type, TypeNode *typeNode,
                          const location &loc, const std::string &context);

void
validateStructDeclShape(AstStructDecl *node);

std::string
describeStructFieldSyntax(AstVarDecl *fieldDecl);

void
validateEmbeddedStructField(AstStructDecl *structDecl, AstVarDecl *fieldDecl,
                            TypeClass *fieldType);

void
insertStructMember(AstStructDecl *structDecl, AstVarDecl *fieldDecl,
                   TypeClass *fieldType,
                   llvm::StringMap<StructType::ValueTy> &members,
                   llvm::StringMap<AccessKind> &memberAccess,
                   llvm::StringSet<> &embeddedMembers,
                   std::unordered_map<std::string, location> &seenMembers,
                   int &nextMemberIndex);

void
validateStructFieldType(AstStructDecl *structDecl, AstVarDecl *fieldDecl,
                        TypeClass *fieldType);

TypeClass *
resolveTypeNode(TypeTable *typeMgr, const CompilationUnit *unit,
                TypeNode *node);

void
rejectBareFunctionType(TypeClass *type, TypeNode *node,
                       const std::string &context,
                       const location &loc = location());

TypeTable *
requireTypeTable(Scope *scope);

void
declareModuleNamespace(Scope &scope, const CompilationUnit &unit);

void
validateExternCFunctionSignature(AstFuncDecl *node, StructType *methodParent,
                                 const std::vector<TypeClass *> &argTypes,
                                 TypeClass *retType);

void
validateExternCType(AstFuncDecl *node, StructType *methodParent,
                    const std::string &role, const std::string &bindingName,
                    TypeClass *type, TypeNode *typeNode, const location &loc);

StructType *
declareStructType(TypeTable *typeMgr, AstStructDecl *node,
                  CompilationUnit *unit = nullptr,
                  bool exportNamespace = false);

Function *
declareFunction(Scope &scope, TypeTable *typeMgr, AstFuncDecl *node,
                StructType *methodParent, CompilationUnit *unit = nullptr,
                bool exportNamespace = false);

Function *
declareExtensionFunction(Scope &scope, TypeTable *typeMgr,
                         AstExtendDecl *extendDecl, AstFuncDecl *node,
                         CompilationUnit *unit = nullptr,
                         bool exportNamespace = false);

}  // namespace declarationsupport_impl

namespace moduleinterface_impl {

void
ensureUnitInterfaceCollected(CompilationUnit &unit);

}  // namespace moduleinterface_impl

}  // namespace lona
