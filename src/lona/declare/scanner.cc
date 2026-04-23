#include "lona/ast/astnode.hh"
#include "lona/ast/type_node_string.hh"
#include "lona/ast/type_node_tools.hh"
#include "lona/declare/support.hh"
#include "lona/err/err.hh"
#include "lona/module/compilation_unit.hh"
#include "lona/type/buildin.hh"
#include "lona/type/scope.hh"
#include "lona/visitor.hh"
#include <cassert>
#include <list>
#include <string>
#include <unordered_map>

namespace lona {

using declarationsupport_impl::declareFunction;
using declarationsupport_impl::declareExtensionFunction;
using declarationsupport_impl::declareStructType;
using declarationsupport_impl::describeStructFieldSyntax;
using declarationsupport_impl::insertStructMember;
using declarationsupport_impl::recordTopLevelDeclName;
using declarationsupport_impl::rejectBareFunctionType;
using declarationsupport_impl::rejectOpaqueStructByValue;
using declarationsupport_impl::requireTypeTable;
using declarationsupport_impl::resolveTypeNode;
using declarationsupport_impl::TopLevelDeclKind;
using declarationsupport_impl::validateEmbeddedStructField;
using declarationsupport_impl::validateExternCType;
using declarationsupport_impl::validateStructDeclShape;
using declarationsupport_impl::validateStructFieldType;

namespace {

bool containsGenericTypeParamReference(
    TypeNode *node, const std::unordered_set<std::string> &params) {
    if (!node) {
        return false;
    }
    if (auto *param = dynamic_cast<FuncParamTypeNode *>(node)) {
        return containsGenericTypeParamReference(param->type, params);
    }
    if (auto *base = dynamic_cast<BaseTypeNode *>(node)) {
        std::string moduleName;
        std::string memberName;
        auto rawName = baseTypeName(base);
        return !splitBaseTypeName(base, moduleName, memberName) &&
               params.contains(rawName);
    }
    if (auto *applied = dynamic_cast<AppliedTypeNode *>(node)) {
        if (containsGenericTypeParamReference(applied->base, params)) {
            return true;
        }
        for (auto *arg : applied->args) {
            if (containsGenericTypeParamReference(arg, params)) {
                return true;
            }
        }
        return false;
    }
    if (auto *qualified = dynamic_cast<ConstTypeNode *>(node)) {
        return containsGenericTypeParamReference(qualified->base, params);
    }
    if (auto *dynType = dynamic_cast<DynTypeNode *>(node)) {
        return containsGenericTypeParamReference(dynType->base, params);
    }
    if (auto *pointer = dynamic_cast<PointerTypeNode *>(node)) {
        return containsGenericTypeParamReference(pointer->base, params);
    }
    if (auto *indexable = dynamic_cast<IndexablePointerTypeNode *>(node)) {
        return containsGenericTypeParamReference(indexable->base, params);
    }
    if (auto *array = dynamic_cast<ArrayTypeNode *>(node)) {
        return containsGenericTypeParamReference(array->base, params);
    }
    if (auto *tuple = dynamic_cast<TupleTypeNode *>(node)) {
        for (auto *item : tuple->items) {
            if (containsGenericTypeParamReference(item, params)) {
                return true;
            }
        }
        return false;
    }
    if (auto *func = dynamic_cast<FuncPtrTypeNode *>(node)) {
        for (auto *arg : func->args) {
            if (containsGenericTypeParamReference(arg, params)) {
                return true;
            }
        }
        return containsGenericTypeParamReference(func->ret, params);
    }
    return false;
}

void validateExternCGenericPointerLeaf(
    AstFuncDecl *node, TypeNode *typeNode,
    const std::unordered_set<std::string> &params,
    const std::string &subject, bool allowGenericLeaf = false) {
    if (!typeNode) {
        return;
    }
    validateTypeNodeLayout(typeNode);
    if (auto *param = dynamic_cast<FuncParamTypeNode *>(typeNode)) {
        validateExternCGenericPointerLeaf(node, param->type, params, subject,
                                          allowGenericLeaf);
        return;
    }
    if (auto *base = dynamic_cast<BaseTypeNode *>(typeNode)) {
        std::string moduleName;
        std::string memberName;
        auto rawName = baseTypeName(base);
        if (!splitBaseTypeName(base, moduleName, memberName) &&
            params.contains(rawName) && !allowGenericLeaf) {
            error(typeNode->loc,
                  "#[extern \"C\"] generic function `" +
                      toStdString(node->name) + "` uses unsupported " +
                      subject + ": " + describeTypeNode(typeNode, "void"),
                  "Generic C FFI type parameters must appear as bare pointer "
                  "targets like `T*`, `T const*`, or `T[*]` so every "
                  "specialization shares one C symbol.");
        }
        return;
    }
    if (auto *qualified = dynamic_cast<ConstTypeNode *>(typeNode)) {
        validateExternCGenericPointerLeaf(node, qualified->base, params,
                                          subject, allowGenericLeaf);
        return;
    }
    if (auto *pointer = dynamic_cast<PointerTypeNode *>(typeNode)) {
        validateExternCGenericPointerLeaf(node, pointer->base, params, subject,
                                          true);
        return;
    }
    if (auto *indexable = dynamic_cast<IndexablePointerTypeNode *>(typeNode)) {
        validateExternCGenericPointerLeaf(node, indexable->base, params,
                                          subject, true);
        return;
    }
    if (auto *applied = dynamic_cast<AppliedTypeNode *>(typeNode)) {
        validateExternCGenericPointerLeaf(node, applied->base, params, subject,
                                          false);
        for (auto *arg : applied->args) {
            validateExternCGenericPointerLeaf(node, arg, params, subject,
                                              false);
        }
        return;
    }
    if (auto *dynType = dynamic_cast<DynTypeNode *>(typeNode)) {
        validateExternCGenericPointerLeaf(node, dynType->base, params, subject,
                                          false);
        return;
    }
    if (auto *array = dynamic_cast<ArrayTypeNode *>(typeNode)) {
        validateExternCGenericPointerLeaf(node, array->base, params, subject,
                                          false);
        return;
    }
    if (auto *tuple = dynamic_cast<TupleTypeNode *>(typeNode)) {
        for (auto *item : tuple->items) {
            validateExternCGenericPointerLeaf(node, item, params, subject,
                                              false);
        }
        return;
    }
    if (auto *func = dynamic_cast<FuncPtrTypeNode *>(typeNode)) {
        for (auto *arg : func->args) {
            validateExternCGenericPointerLeaf(node, arg, params, subject,
                                              false);
        }
        validateExternCGenericPointerLeaf(node, func->ret, params, subject,
                                          false);
    }
}

void validateExternCGenericFunctionDecl(TypeTable *typeMgr,
                                        CompilationUnit *unit,
                                        AstFuncDecl *node) {
    if (!node || !node->isExternC() || !node->hasTypeParams()) {
        return;
    }

    std::unordered_set<std::string> genericParamNames;
    if (node->typeParams) {
        for (auto *param : *node->typeParams) {
            if (!param) {
                continue;
            }
            genericParamNames.insert(toStdString(param->name.text));
        }
    }
    if (genericParamNames.empty()) {
        return;
    }

    auto funcName = toStdString(node->name);
    if (node->hasBody()) {
        error(node->loc,
              "#[extern \"C\"] generic function `" + funcName +
                  "` cannot have a body",
              "Declare it as a bodyless import. Generic C FFI v0 shares one "
              "C symbol across all type arguments.");
    }
    if (node->typeParams) {
        for (auto *param : *node->typeParams) {
            if (!param || !param->hasBoundTrait()) {
                continue;
            }
            error(param->name.loc,
                  "#[extern \"C\"] generic function `" + funcName +
                      "` does not support trait bound on `" +
                      toStdString(param->name.text) + "`",
                  "Use bare type parameters like `[T]`. Generic C FFI erases "
                  "type arguments at the C boundary.");
        }
    }
    if (node->args) {
        for (auto *arg : *node->args) {
            auto *varDecl = dynamic_cast<AstVarDecl *>(arg);
            if (!varDecl) {
                continue;
            }
            if (varDecl->bindingKind == BindingKind::Ref) {
                error(varDecl->loc,
                      "#[extern \"C\"] generic function `" + funcName +
                          "` parameter `" + toStdString(varDecl->field) +
                          "` cannot use `ref` binding",
                      "Use an explicit pointer type like `i32*` instead.");
            }
            if (containsGenericTypeParamReference(varDecl->typeNode,
                                                  genericParamNames)) {
                validateExternCGenericPointerLeaf(
                    node, varDecl->typeNode, genericParamNames,
                    "parameter `" + toStdString(varDecl->field) + "`");
                continue;
            }
            auto *type = resolveTypeNode(typeMgr, unit, varDecl->typeNode);
            if (!type) {
                error(varDecl->loc,
                      "unknown type for function parameter `" +
                          toStdString(varDecl->field) + "` in `" + funcName +
                          "`: " + describeTypeNode(varDecl->typeNode, "void"));
            }
            rejectOpaqueStructByValue(
                type, varDecl->typeNode, varDecl->loc,
                "parameter `" + toStdString(varDecl->field) +
                    "` in function `" + funcName + "`");
            validateExternCType(node, nullptr, "parameter",
                                toStdString(varDecl->field), type,
                                varDecl->typeNode, varDecl->loc);
        }
    }
    if (!node->retType) {
        return;
    }
    if (containsGenericTypeParamReference(node->retType, genericParamNames)) {
        validateExternCGenericPointerLeaf(node, node->retType,
                                          genericParamNames, "return type");
        return;
    }
    auto *retType = resolveTypeNode(typeMgr, unit, node->retType);
    if (!retType) {
        error(node->loc,
              "unknown return type for function `" + funcName +
                  "`: " + describeTypeNode(node->retType, "void"));
    }
    rejectOpaqueStructByValue(retType, node->retType, node->loc,
                              "return type of function `" + funcName + "`");
    validateExternCType(node, nullptr, "return type", std::string(), retType,
                        node->retType, node->loc);
}

}  // namespace

class StructVisitor : public AstVisitorAny {
    TypeTable *typeMgr;
    CompilationUnit *unit;
    bool exportNamespace;
    AstStructDecl *structDecl = nullptr;

    llvm::StringMap<StructType::ValueTy> members;
    llvm::StringMap<AccessKind> memberAccess;
    llvm::StringSet<> embeddedMembers;
    std::unordered_map<std::string, location> seenMembers;
    int nextMemberIndex = 0;

    using AstVisitorAny::visit;

    Object *visit(AstStatList *node) override {
        for (auto it = node->getBody().begin(); it != node->getBody().end();
             it++) {
            (*it)->accept(*this);
        }
        return nullptr;
    }

    Object *visit(AstVarDecl *node) override {
        if (node->bindingKind == BindingKind::Ref) {
            error(node->loc,
                  "struct fields cannot use `ref` binding for `" +
                      describeStructFieldSyntax(node) + "`",
                  "Store an explicit pointer type instead. Struct fields must "
                  "be value or pointer-like storage.");
        }
        auto *type = resolveTypeNode(typeMgr, unit, node->typeNode);
        if (!type) {
            error(node->loc, "unknown struct field type for `" +
                                 describeStructFieldSyntax(node) + "`: " +
                                 describeTypeNode(node->typeNode, "void"));
        }
        rejectBareFunctionType(
            type, node->typeNode,
            "unsupported bare function struct field type for `" +
                describeStructFieldSyntax(node) + "`",
            node->loc);
        validateStructFieldType(structDecl, node, type);
        validateEmbeddedStructField(structDecl, node, type);
        insertStructMember(structDecl, node, type, members, memberAccess,
                           embeddedMembers, seenMembers, nextMemberIndex);

        return nullptr;
    }

public:
    StructVisitor(TypeTable *typeMgr, AstStructDecl *node,
                  CompilationUnit *unit = nullptr, bool exportNamespace = false)
        : typeMgr(typeMgr),
          unit(unit),
          exportNamespace(exportNamespace),
          structDecl(node) {
        auto *lostructTy =
            declareStructType(typeMgr, node, unit, exportNamespace);
        assert(lostructTy);

        if (!lostructTy->isOpaque()) {
            return;
        }

        if (!node->body) {
            return;
        }

        this->visit(node->body);
        lostructTy->complete(members, memberAccess, embeddedMembers);
    }
};

class TypeCollector : public AstVisitorAny {
    TypeTable *typeMgr;
    Scope *scope;
    CompilationUnit *unit;
    bool exportNamespace;

    std::list<AstStructDecl *> structDecls;
    std::list<AstFuncDecl *> funcDecls;
    std::unordered_map<std::string, std::pair<TopLevelDeclKind, location>>
        topLevelDecls;

    using AstVisitorAny::visit;

    Object *visit(AstProgram *node) override {
        this->visit(node->body);
        return nullptr;
    }

    Object *visit(AstStatList *node) override {
        for (auto *it : node->body) {
            if (it->is<AstStructDecl>()) {
                auto *decl = it->as<AstStructDecl>();
                validateStructDeclShape(decl);
                recordTopLevelDeclName(topLevelDecls, toStdString(decl->name),
                                       TopLevelDeclKind::StructType, decl->loc);
                structDecls.push_back(it->as<AstStructDecl>());
            } else if (it->is<AstTraitDecl>()) {
                auto *decl = it->as<AstTraitDecl>();
                recordTopLevelDeclName(topLevelDecls, toStdString(decl->name),
                                       TopLevelDeclKind::Trait, decl->loc);
            } else if (it->is<AstFuncDecl>()) {
                auto *decl = it->as<AstFuncDecl>();
                if (!decl->hasExtensionReceiver()) {
                    recordTopLevelDeclName(topLevelDecls,
                                           toStdString(decl->name),
                                           TopLevelDeclKind::Function,
                                           decl->loc);
                }
                funcDecls.push_back(it->as<AstFuncDecl>());
            }
        }
        return nullptr;
    }

    Object *visit(AstStructDecl *node) override {
        if (node->hasTypeParams()) {
            return nullptr;
        }
        auto *structTy =
            declareStructType(typeMgr, node, unit, exportNamespace);
        if (!node->body || !node->body->is<AstStatList>()) {
            return nullptr;
        }
        for (auto *stmt : node->body->as<AstStatList>()->getBody()) {
            auto *func = stmt->as<AstFuncDecl>();
            if (!func) {
                continue;
            }
            declareFunction(*scope, typeMgr, func, structTy, unit,
                            exportNamespace);
        }
        return nullptr;
    }

    Object *visit(AstFuncDecl *node) override {
        if (node->hasTypeParams()) {
            validateExternCGenericFunctionDecl(typeMgr, unit, node);
            return nullptr;
        }
        if (node->hasExtensionReceiver()) {
            declareExtensionFunction(*scope, typeMgr, node, unit,
                                     exportNamespace);
            return nullptr;
        }
        declareFunction(*scope, typeMgr, node, nullptr, unit, exportNamespace);
        return nullptr;
    }

public:
    TypeCollector(TypeTable *typeMgr, Scope *scope, AstNode *root,
                  CompilationUnit *unit = nullptr, bool exportNamespace = false)
        : typeMgr(typeMgr),
          scope(scope),
          unit(unit),
          exportNamespace(exportNamespace) {
        this->visit(root);

        for (auto *it : structDecls) {
            declareStructType(typeMgr, it, unit, exportNamespace);
        }

        for (auto *it : structDecls) {
            if (!it->hasTypeParams()) {
                StructVisitor(typeMgr, it, unit, exportNamespace);
            }
        }

        for (auto *it : structDecls) {
            this->visit(it);
        }

        for (auto *it : funcDecls) {
            this->visit(it);
        }
    }
};

Function *
createFunc(Scope &scope, AstFuncDecl *root, StructType *parent) {
    initBuildinType(&scope);
    auto *func = declareFunction(scope, requireTypeTable(&scope), root, parent,
                                 nullptr, false);
    return func;
}

void
scanningType(Scope *global, AstNode *root) {
    initBuildinType(global);
    TypeCollector(requireTypeTable(global), global, root, nullptr, false);
}

StructType *
createStruct(Scope *scope, AstStructDecl *node) {
    initBuildinType(scope);
    auto *typeMgr = requireTypeTable(scope);
    auto *type = declareStructType(typeMgr, node, nullptr, false);
    if (type->isOpaque()) {
        StructVisitor(typeMgr, node, nullptr, false);
    }
    return type;
}

}  // namespace lona
