#include "lona/declare/support.hh"

#include "lona/abi/abi.hh"
#include "lona/ast/type_node_string.hh"
#include "lona/ast/type_node_tools.hh"
#include "lona/err/err.hh"
#include "lona/sema/initializer.hh"
#include "lona/sema/moduleentry.hh"
#include "lona/type/buildin.hh"

namespace lona {
namespace declarationsupport_impl {

namespace {

[[noreturn]] void
reportLocalFunctionConflict(AstFuncDecl *node, llvm::StringRef functionName,
                            FuncType *existingType, FuncType *incomingType) {
    std::string message =
        "conflicting declarations for function `" + functionName.str() + "`";
    if (existingType && incomingType) {
        message += ": `" + describeResolvedType(existingType) + "` vs `" +
                   describeResolvedType(incomingType) + "`";
    }
    error(node ? node->loc : location(), std::move(message),
          "Make duplicated `#[extern \"C\"]` function declarations use the "
          "same signature in every module.");
}

std::string
describeExternCFunctionName(AstFuncDecl *node, StructType *methodParent) {
    if (!node) {
        return "<unknown>";
    }
    auto name = toStdString(node->name);
    if (!methodParent) {
        return name;
    }
    return toStdString(methodParent->full_name) + "." + name;
}

std::string
buildAppliedStructTypeName(llvm::StringRef templateName,
                           const std::vector<TypeClass *> &typeArgs) {
    std::string name = templateName.str() + "[";
    for (std::size_t i = 0; i < typeArgs.size(); ++i) {
        if (i != 0) {
            name += ", ";
        }
        auto *typeArg = typeArgs[i];
        name += typeArg ? toStdString(typeArg->full_name) : "<null>";
    }
    name += "]";
    return name;
}

std::string
describeExternCTypeSubject(const std::string &role, const std::string &name) {
    if (name.empty()) {
        return role;
    }
    return role + " `" + name + "`";
}

std::string
describeExternCType(TypeClass *type, TypeNode *node) {
    if (node) {
        return describeTypeNode(node,
                                type ? toStdString(type->full_name) : "void");
    }
    if (type) {
        return toStdString(type->full_name);
    }
    return "void";
}

bool
isExternCCallbackType(TypeClass *type) {
    if (!type) {
        return false;
    }
    if (auto *qualified = type->as<ConstType>()) {
        return isExternCCallbackType(qualified->getBaseType());
    }
    if (auto *pointer = asUnqualified<PointerType>(type)) {
        auto *pointeeType = pointer->getPointeeType();
        return (pointeeType && pointeeType->as<FuncType>()) ||
               isExternCCallbackType(pointeeType);
    }
    if (auto *indexable = asUnqualified<IndexablePointerType>(type)) {
        auto *elementType = indexable->getElementType();
        return (elementType && elementType->as<FuncType>()) ||
               isExternCCallbackType(elementType);
    }
    if (auto *array = asUnqualified<ArrayType>(type)) {
        return isExternCCallbackType(array->getElementType());
    }
    return false;
}

bool
isExternCByValueAggregateType(TypeClass *type) {
    auto *storageType = stripTopLevelConst(type);
    return storageType &&
           (storageType->as<StructType>() || storageType->as<TupleType>() ||
            storageType->as<ArrayType>());
}

bool
isExternCTraitObjectType(TypeClass *type) {
    auto *storageType = stripTopLevelConst(type);
    return storageType && storageType->as<DynTraitType>();
}

bool
isCCompatibleStructIdentity(StructType *type) {
    return type && (type->isOpaque() || type->isReprC());
}

bool
isCCompatiblePointerTarget(TypeClass *type) {
    if (!type) {
        return false;
    }
    if (auto *qualified = type->as<ConstType>()) {
        return isCCompatiblePointerTarget(qualified->getBaseType());
    }
    if (type->as<BaseType>()) {
        return true;
    }
    if (auto *structType = type->as<StructType>()) {
        return isCCompatibleStructIdentity(structType);
    }
    if (auto *pointerType = type->as<PointerType>()) {
        auto *pointeeType = pointerType->getPointeeType();
        return pointeeType && !pointeeType->as<FuncType>() &&
               isCCompatiblePointerTarget(pointeeType);
    }
    if (auto *indexableType = type->as<IndexablePointerType>()) {
        auto *elementType = indexableType->getElementType();
        return elementType && !elementType->as<FuncType>() &&
               isCCompatiblePointerTarget(elementType);
    }
    return false;
}

void
validateExternCTypeImpl(AstFuncDecl *node, StructType *methodParent,
                        const std::string &role, const std::string &bindingName,
                        TypeClass *type, TypeNode *typeNode,
                        const location &loc) {
    if (!node || !node->isExternC() || !type) {
        return;
    }

    auto funcName = describeExternCFunctionName(node, methodParent);
    auto subject = describeExternCTypeSubject(role, bindingName);
    auto typeName = describeExternCType(type, typeNode);

    if (isExternCCallbackType(type)) {
        error(loc,
              "#[extern \"C\"] function `" + funcName + "` uses unsupported " +
                  subject + ": " + typeName,
              "Callback support is not implemented in C FFI v0 yet.");
    }
    if (isExternCTraitObjectType(type)) {
        error(loc,
              "#[extern \"C\"] function `" + funcName + "` uses unsupported " +
                  subject + ": " + typeName,
              "Trait objects are internal runtime values in trait v0. Pass "
              "an explicit opaque pointer type across the C boundary "
              "instead.");
    }
    if (auto *pointerType = asUnqualified<PointerType>(type)) {
        if (!isCCompatiblePointerTarget(pointerType->getPointeeType())) {
            error(loc,
                  "#[extern \"C\"] function `" + funcName +
                      "` uses unsupported " + subject + ": " + typeName,
                  "Use pointers to scalars, pointers, opaque `struct` "
                  "declarations, or `#[repr \"C\"] struct` types. Ordinary "
                  "Lona structs cannot cross the C FFI boundary.");
        }
        return;
    }
    if (auto *indexableType = asUnqualified<IndexablePointerType>(type)) {
        if (!isCCompatiblePointerTarget(indexableType->getElementType())) {
            error(loc,
                  "#[extern \"C\"] function `" + funcName +
                      "` uses unsupported " + subject + ": " + typeName,
                  "Use pointers to scalars, pointers, opaque `struct` "
                  "declarations, or `#[repr \"C\"] struct` types. Ordinary "
                  "Lona structs cannot cross the C FFI boundary.");
        }
        return;
    }
    if (asUnqualified<TupleType>(type)) {
        error(loc,
              "#[extern \"C\"] function `" + funcName + "` uses unsupported " +
                  subject + ": " + typeName,
              "Flatten the tuple into scalar parameters or pass a pointer "
              "instead.");
    }
    if (isExternCByValueAggregateType(type)) {
        error(loc,
              "#[extern \"C\"] function `" + funcName + "` uses unsupported " +
                  subject + ": " + typeName,
              "Pass a pointer instead. C FFI v0 does not support aggregate "
              "values at the boundary yet.");
    }
}

std::string
resolveTopLevelName(const CompilationUnit *unit, const string &name,
                    bool exportNamespace) {
    auto resolved = toStdString(name);
    if (!unit || !exportNamespace) {
        return resolved;
    }
    return toStdString(unit->exportNamespacePrefix() + "." + name);
}

std::string
resolveFunctionSymbolName(const CompilationUnit *unit, const string &name,
                          AbiKind abiKind, bool exportNamespace,
                          Scope *scope = nullptr) {
    if (abiKind == AbiKind::C) {
        return toStdString(name);
    }
    auto resolved = resolveTopLevelName(unit, name, exportNamespace);
    if (scope && unit && !exportNamespace &&
        scope->getObj(llvm::StringRef(resolved))) {
        return toStdString(unit->exportNamespacePrefix() + "." + name);
    }
    return resolved;
}

bool
isBuiltinScalarExtensionBase(TypeClass *type) {
    auto *storageType = stripTopLevelConst(type);
    return storageType && (storageType == u8Ty || storageType == i8Ty ||
                           storageType == u16Ty || storageType == i16Ty ||
                           storageType == u32Ty || storageType == i32Ty ||
                           storageType == u64Ty || storageType == i64Ty ||
                           storageType == usizeTy || storageType == f32Ty ||
                           storageType == f64Ty || storageType == boolTy);
}

bool
containsSelfTypeReference(TypeNode *node) {
    if (!node) {
        return false;
    }
    if (auto *param = dynamic_cast<FuncParamTypeNode *>(node)) {
        return containsSelfTypeReference(param->type);
    }
    if (auto *base = dynamic_cast<BaseTypeNode *>(node)) {
        std::string moduleName;
        std::string memberName;
        return !splitBaseTypeName(base, moduleName, memberName) &&
               baseTypeName(base) == "Self";
    }
    if (auto *applied = dynamic_cast<AppliedTypeNode *>(node)) {
        if (containsSelfTypeReference(applied->base)) {
            return true;
        }
        for (auto *arg : applied->args) {
            if (containsSelfTypeReference(arg)) {
                return true;
            }
        }
        return false;
    }
    if (auto *qualified = dynamic_cast<ConstTypeNode *>(node)) {
        return containsSelfTypeReference(qualified->base);
    }
    if (auto *dynType = dynamic_cast<DynTypeNode *>(node)) {
        return containsSelfTypeReference(dynType->base);
    }
    if (auto *pointer = dynamic_cast<PointerTypeNode *>(node)) {
        return containsSelfTypeReference(pointer->base);
    }
    if (auto *indexable = dynamic_cast<IndexablePointerTypeNode *>(node)) {
        return containsSelfTypeReference(indexable->base);
    }
    if (auto *array = dynamic_cast<ArrayTypeNode *>(node)) {
        return containsSelfTypeReference(array->base);
    }
    if (auto *tuple = dynamic_cast<TupleTypeNode *>(node)) {
        for (auto *item : tuple->items) {
            if (containsSelfTypeReference(item)) {
                return true;
            }
        }
        return false;
    }
    if (auto *func = dynamic_cast<FuncPtrTypeNode *>(node)) {
        for (auto *arg : func->args) {
            if (containsSelfTypeReference(arg)) {
                return true;
            }
        }
        return containsSelfTypeReference(func->ret);
    }
    return false;
}

TypeClass *
resolveSelfAwareTypeNode(TypeTable *typeMgr, const CompilationUnit *unit,
                         TypeNode *node, TypeClass *selfType) {
    if (!node || !selfType || !containsSelfTypeReference(node)) {
        return resolveTypeNode(typeMgr, unit, node);
    }
    if (auto *param = dynamic_cast<FuncParamTypeNode *>(node)) {
        return resolveSelfAwareTypeNode(typeMgr, unit, param->type, selfType);
    }
    if (auto *base = dynamic_cast<BaseTypeNode *>(node)) {
        std::string moduleName;
        std::string memberName;
        return !splitBaseTypeName(base, moduleName, memberName) &&
                       baseTypeName(base) == "Self"
                   ? selfType
                   : resolveTypeNode(typeMgr, unit, node);
    }
    if (auto *qualified = dynamic_cast<ConstTypeNode *>(node)) {
        auto *base =
            resolveSelfAwareTypeNode(typeMgr, unit, qualified->base, selfType);
        return base ? typeMgr->createConstType(base) : nullptr;
    }
    if (auto *pointer = dynamic_cast<PointerTypeNode *>(node)) {
        auto *type =
            resolveSelfAwareTypeNode(typeMgr, unit, pointer->base, selfType);
        for (std::uint32_t i = 0; type && i < pointer->dim; ++i) {
            type = typeMgr->createPointerType(type);
        }
        return type;
    }
    if (auto *indexable = dynamic_cast<IndexablePointerTypeNode *>(node)) {
        auto *base =
            resolveSelfAwareTypeNode(typeMgr, unit, indexable->base, selfType);
        return base ? typeMgr->createIndexablePointerType(base) : nullptr;
    }
    if (auto *array = dynamic_cast<ArrayTypeNode *>(node)) {
        auto *base =
            resolveSelfAwareTypeNode(typeMgr, unit, array->base, selfType);
        return base ? typeMgr->createArrayType(base, array->dim) : nullptr;
    }
    if (auto *tuple = dynamic_cast<TupleTypeNode *>(node)) {
        std::vector<TypeClass *> items;
        items.reserve(tuple->items.size());
        for (auto *item : tuple->items) {
            auto *type =
                resolveSelfAwareTypeNode(typeMgr, unit, item, selfType);
            if (!type) {
                return nullptr;
            }
            items.push_back(type);
        }
        return typeMgr->getOrCreateTupleType(items);
    }
    if (auto *func = dynamic_cast<FuncPtrTypeNode *>(node)) {
        std::vector<TypeClass *> args;
        std::vector<BindingKind> bindingKinds;
        args.reserve(func->args.size());
        bindingKinds.reserve(func->args.size());
        for (auto *arg : func->args) {
            bindingKinds.push_back(funcParamBindingKind(arg));
            auto *type = resolveSelfAwareTypeNode(
                typeMgr, unit, unwrapFuncParamType(arg), selfType);
            if (!type) {
                return nullptr;
            }
            args.push_back(type);
        }
        auto *ret =
            resolveSelfAwareTypeNode(typeMgr, unit, func->ret, selfType);
        auto *funcType = typeMgr->getOrCreateFunctionType(
            args, ret, std::move(bindingKinds));
        return funcType ? typeMgr->createPointerType(funcType) : nullptr;
    }
    return nullptr;
}

}  // namespace

TypeClass *
resolveContextualSelfType(TypeTable *typeMgr, const CompilationUnit *unit,
                          TypeNode *node, TypeClass *selfType) {
    return resolveSelfAwareTypeNode(typeMgr, unit, node, selfType);
}

void
validateExtensionTargetShape(AstExtendDecl *node) {
    if (!node || !node->targetType) {
        return;
    }
    auto *target = node->targetType;
    if (dynamic_cast<ConstTypeNode *>(target) ||
        dynamic_cast<PointerTypeNode *>(target) ||
        dynamic_cast<IndexablePointerTypeNode *>(target) ||
        dynamic_cast<ArrayTypeNode *>(target) ||
        dynamic_cast<TupleTypeNode *>(target) ||
        dynamic_cast<FuncPtrTypeNode *>(target) ||
        dynamic_cast<DynTypeNode *>(target)) {
        error(node->loc,
              "unsupported extend target `" + describeTypeNode(target, "void") +
                  "`",
              "`extend` targets a concrete struct or builtin scalar; "
              "receiver borrowing is selected by `def`, `set def`, or "
              "`var def` inside the block.");
    }
}

void
validateExternCType(AstFuncDecl *node, StructType *methodParent,
                    const std::string &role, const std::string &bindingName,
                    TypeClass *type, TypeNode *typeNode, const location &loc) {
    validateExternCTypeImpl(node, methodParent, role, bindingName, type,
                            typeNode, loc);
}

std::string
resolveStructMethodOwnerTypeName(StructType *methodParent) {
    if (!methodParent) {
        return {};
    }
    if (methodParent->isAppliedTemplateInstance() &&
        !methodParent->getAppliedTemplateName().empty()) {
        return buildAppliedStructTypeName(
            llvm::StringRef(methodParent->getAppliedTemplateName().tochara(),
                            methodParent->getAppliedTemplateName().size()),
            methodParent->getAppliedTypeArgs());
    }
    return toStdString(methodParent->full_name);
}

std::string
resolveStructMethodSymbolName(StructType *methodParent,
                              llvm::StringRef methodName,
                              ReceiverMode receiverMode) {
    if (!methodParent) {
        return methodName.str();
    }
    auto ownerTypeName = resolveStructMethodOwnerTypeName(methodParent);
    if (methodParent->isAppliedTemplateInstance()) {
        return mangleModuleEntryComponent(string(ownerTypeName)) + "." +
               methodName.str() + ".__receiver_" +
               receiverModeKeyword(receiverMode);
    }
    return ownerTypeName + "." + methodName.str() + ".__receiver_" +
           receiverModeKeyword(receiverMode);
}

std::string
resolveTraitMethodSymbolName(StructType *methodParent,
                             llvm::StringRef traitName,
                             llvm::StringRef methodName,
                             ReceiverMode receiverMode) {
    if (!methodParent) {
        return methodName.str();
    }
    return resolveStructMethodOwnerTypeName(methodParent) + ".__trait__." +
           mangleModuleEntryComponent(traitName) + "." + methodName.str() +
           ".__receiver_" + receiverModeKeyword(receiverMode);
}

std::vector<string>
extractParamNames(AstFuncDecl *node, std::size_t skipLeadingArgs) {
    std::vector<string> names;
    if (!node || !node->args) {
        return names;
    }
    if (skipLeadingArgs >= node->args->size()) {
        return names;
    }
    names.reserve(node->args->size() - skipLeadingArgs);
    std::size_t index = 0;
    for (auto *arg : *node->args) {
        if (index++ < skipLeadingArgs) {
            continue;
        }
        auto *varDecl = dynamic_cast<AstVarDecl *>(arg);
        if (!varDecl) {
            continue;
        }
        names.push_back(varDecl->field);
    }
    return names;
}

std::vector<BindingKind>
extractParamBindingKinds(AstFuncDecl *node, std::size_t skipLeadingArgs,
                         bool prependImplicitSelf) {
    std::vector<BindingKind> kinds;
    if (prependImplicitSelf) {
        kinds.push_back(BindingKind::Value);
    }
    if (!node || !node->args) {
        return kinds;
    }
    if (skipLeadingArgs >= node->args->size()) {
        return kinds;
    }
    kinds.reserve(kinds.size() + node->args->size() - skipLeadingArgs);
    std::size_t index = 0;
    for (auto *arg : *node->args) {
        if (index++ < skipLeadingArgs) {
            continue;
        }
        auto *varDecl = dynamic_cast<AstVarDecl *>(arg);
        kinds.push_back(varDecl ? varDecl->bindingKind : BindingKind::Value);
    }
    return kinds;
}

std::string
resolveExtensionMethodSymbolName(const CompilationUnit *unit,
                                 const std::string &targetTypeSpelling,
                                 llvm::StringRef methodName,
                                 ReceiverMode receiverMode,
                                 bool exportNamespace) {
    (void)exportNamespace;
    auto receiverKey = mangleModuleEntryComponent(string(targetTypeSpelling));
    auto prefix =
        unit ? toStdString(unit->exportNamespacePrefix()) : std::string();
    auto mode = std::string(receiverModeKeyword(receiverMode));
    if (prefix.empty()) {
        return receiverKey + ".__extend__." + mode + "." + methodName.str();
    }
    return prefix + "." + receiverKey + ".__extend__." + mode + "." +
           methodName.str();
}

TypeClass *
extensionReceiverType(TypeTable *typeMgr, TypeClass *targetType,
                      ReceiverMode receiverMode) {
    if (!typeMgr || !targetType) {
        return nullptr;
    }
    if (receiverMode == ReceiverMode::Value) {
        return targetType;
    }
    auto *pointee = receiverMode == ReceiverMode::BorrowedReadWrite
                        ? targetType
                        : typeMgr->createConstType(targetType);
    return typeMgr->createPointerType(pointee);
}

TypeClass *
interfaceExtensionReceiverType(ModuleInterface *interface,
                               TypeClass *targetType,
                               ReceiverMode receiverMode) {
    if (!interface || !targetType) {
        return nullptr;
    }
    if (receiverMode == ReceiverMode::Value) {
        return targetType;
    }
    auto *pointee = receiverMode == ReceiverMode::BorrowedReadWrite
                        ? targetType
                        : interface->getOrCreateConstType(targetType);
    return interface->getOrCreatePointerType(pointee);
}

TypeClass *
methodReceiverType(TypeTable *typeMgr, StructType *methodParent,
                   ReceiverMode receiverMode) {
    if (!typeMgr || !methodParent) {
        return nullptr;
    }
    if (receiverMode == ReceiverMode::Value) {
        return methodParent;
    }
    auto *pointee =
        receiverMode == ReceiverMode::BorrowedReadWrite
            ? static_cast<TypeClass *>(methodParent)
            : static_cast<TypeClass *>(typeMgr->createConstType(methodParent));
    return typeMgr->createPointerType(pointee);
}

TypeClass *
interfaceMethodReceiverType(ModuleInterface *interface,
                            StructType *methodParent,
                            ReceiverMode receiverMode) {
    if (!interface || !methodParent) {
        return nullptr;
    }
    if (receiverMode == ReceiverMode::Value) {
        return methodParent;
    }
    auto *pointee = receiverMode == ReceiverMode::BorrowedReadWrite
                        ? static_cast<TypeClass *>(methodParent)
                        : static_cast<TypeClass *>(
                              interface->getOrCreateConstType(methodParent));
    return interface->getOrCreatePointerType(pointee);
}

void
validateFunctionReceiverAccess(AstFuncDecl *node, StructType *methodParent) {
    if (!node || node->receiverMode == ReceiverMode::BorrowedReadOnly) {
        return;
    }
    if (methodParent) {
        if (node->receiverMode == ReceiverMode::Value &&
            methodParent->isOpaqueDecl()) {
            error(node->loc,
                  "`var def` requires a complete receiver type, got opaque `" +
                      toStdString(methodParent->full_name) + "`",
                  "Use a borrowed method or complete the struct layout.");
        }
        return;
    }
    auto modifier = std::string(receiverModeKeyword(node->receiverMode));
    error(node->loc, "`" + modifier + " def` is only valid on methods",
          "Move this declaration into a receiver scope, or remove the `" +
              modifier + "` receiver modifier.");
}

void
validateExternCFunctionSignature(AstFuncDecl *node, StructType *methodParent,
                                 const std::vector<TypeClass *> &argTypes,
                                 TypeClass *retType) {
    if (!node || !node->isExternC()) {
        return;
    }

    auto funcName = describeExternCFunctionName(node, methodParent);
    if (methodParent) {
        error(node->loc,
              "#[extern \"C\"] method `" + funcName + "` is not supported",
              "Declare a top-level wrapper function instead. C FFI v0 only "
              "supports top-level functions.");
    }

    size_t argTypeIndex = 0;
    if (node->args) {
        for (auto *arg : *node->args) {
            auto *varDecl = dynamic_cast<AstVarDecl *>(arg);
            if (!varDecl) {
                continue;
            }
            if (varDecl->bindingKind == BindingKind::Ref) {
                error(varDecl->loc,
                      "#[extern \"C\"] function `" + funcName +
                          "` parameter `" + toStdString(varDecl->field) +
                          "` cannot use `ref` binding",
                      "Use an explicit pointer type like `i32*` instead.");
            }
            auto *argType = argTypeIndex < argTypes.size()
                                ? argTypes[argTypeIndex]
                                : nullptr;
            rejectOpaqueStructByValue(argType, varDecl->typeNode, varDecl->loc,
                                      "parameter `" +
                                          toStdString(varDecl->field) +
                                          "` in function `" + funcName + "`");
            validateExternCTypeImpl(node, methodParent, "parameter",
                                    toStdString(varDecl->field), argType,
                                    varDecl->typeNode, varDecl->loc);
            ++argTypeIndex;
        }
    }

    rejectOpaqueStructByValue(retType, node->retType, node->loc,
                              "return type of function `" + funcName + "`");
    validateExternCTypeImpl(node, methodParent, "return type", std::string(),
                            retType, node->retType, node->loc);
}

Function *
declareFunction(Scope &scope, TypeTable *typeMgr, AstFuncDecl *node,
                StructType *methodParent, CompilationUnit *unit,
                bool exportNamespace) {
    validateFunctionReceiverAccess(node, methodParent);
    if (node && node->hasTypeParams() && methodParent) {
        return nullptr;
    }
    auto &funcName = node->name;
    auto resolvedFunctionName = resolveFunctionSymbolName(
        unit, funcName, node ? node->abiKind : AbiKind::Native, exportNamespace,
        &scope);
    Function *existingFunction = nullptr;
    if (methodParent) {
        if (auto *existing = typeMgr->getMethodFunction(
                methodParent,
                llvm::StringRef(funcName.tochara(), funcName.size()))) {
            return existing;
        }
    } else {
        if (unit) {
            if (unit->importsModule(toStdString(funcName))) {
                error(node->loc,
                      "top-level function `" + toStdString(funcName) +
                          "` conflicts with imported module alias `" +
                          toStdString(funcName) + "`",
                      "Rename the function so `" + toStdString(funcName) +
                          ".xxx` continues to refer to the imported module.");
            }
            if (unit->findLocalType(toStdString(funcName)) != nullptr) {
                error(node->loc,
                      "top-level function `" + toStdString(funcName) +
                          "` conflicts with struct `" + toStdString(funcName) +
                          "`",
                      "Type names reserve constructor syntax like `" +
                          toStdString(funcName) +
                          "(...)`. Rename the function, for example `make" +
                          toStdString(funcName) + "`.");
            }
            unit->bindLocalFunction(toStdString(funcName),
                                    resolvedFunctionName);
        }
        if (auto *existing =
                scope.getObj(llvm::StringRef(resolvedFunctionName))) {
            existingFunction = existing->as<Function>();
            if (!existingFunction) {
                error(node->loc, "top-level function `" +
                                     toStdString(funcName) +
                                     "` conflicts with module namespace `" +
                                     resolvedFunctionName + "`");
            }
        }
    }

    std::vector<TypeClass *> argTypes;
    auto argBindingKinds =
        extractParamBindingKinds(node, 0, methodParent != nullptr);
    TypeClass *retType = nullptr;
    if (node->retType) {
        retType = resolveContextualSelfType(typeMgr, unit, node->retType,
                                            methodParent);
        if (!retType) {
            error(node->loc,
                  "unknown return type for function `" + toStdString(funcName) +
                      "`: " + describeTypeNode(node->retType, "void"));
        }
        rejectBareFunctionType(retType, node->retType,
                               "unsupported bare function return type for `" +
                                   toStdString(funcName) + "`",
                               node->loc);
        rejectOpaqueStructByValue(
            retType, node->retType, node->loc,
            "return type of function `" + toStdString(funcName) + "`");
    }

    if (methodParent) {
        argTypes.push_back(
            methodReceiverType(typeMgr, methodParent, node->receiverMode));
    }

    if (node->args) {
        for (auto *arg : *node->args) {
            if (!arg->is<AstVarDecl>()) {
                error(node->loc, "invalid function parameter declaration in `" +
                                     toStdString(funcName) + "`");
            }
            auto *varDecl = arg->as<AstVarDecl>();
            auto *type = resolveContextualSelfType(
                typeMgr, unit, varDecl->typeNode, methodParent);
            if (!type) {
                error(varDecl->loc,
                      "unknown type for function parameter `" +
                          toStdString(varDecl->field) + "` in `" +
                          toStdString(funcName) +
                          "`: " + describeTypeNode(varDecl->typeNode, "void"));
            }
            rejectBareFunctionType(
                type, varDecl->typeNode,
                "unsupported bare function parameter type for `" +
                    toStdString(varDecl->field) + "` in `" +
                    toStdString(funcName) + "`",
                varDecl->loc);
            rejectOpaqueStructByValue(
                type, varDecl->typeNode, varDecl->loc,
                "parameter `" + toStdString(varDecl->field) +
                    "` in function `" + toStdString(funcName) + "`");
            argTypes.push_back(type);
        }
    }

    validateExternCFunctionSignature(node, methodParent, argTypes, retType);
    auto *funcType = typeMgr->getOrCreateFunctionType(
        argTypes, retType, std::move(argBindingKinds), node->abiKind);
    if (existingFunction) {
        auto *existingType = existingFunction->getType()->as<FuncType>();
        const auto receiverMode =
            methodParent ? std::optional<ReceiverMode>(node->receiverMode)
                         : std::nullopt;
        if (existingType != funcType ||
            existingFunction->receiverMode() != receiverMode) {
            reportLocalFunctionConflict(node, resolvedFunctionName,
                                        existingType, funcType);
        }
        return existingFunction;
    }
    if (methodParent && !methodParent->getMethodType(llvm::StringRef(
                            funcName.tochara(), funcName.size()))) {
        methodParent->addMethodType(
            llvm::StringRef(funcName.tochara(), funcName.size()),
            node->receiverMode, funcType, extractParamNames(node));
    }

    std::string llvmName = resolvedFunctionName.empty() ? toStdString(funcName)
                                                        : resolvedFunctionName;
    if (methodParent) {
        llvmName = resolveStructMethodSymbolName(
            methodParent, llvm::StringRef(funcName.tochara(), funcName.size()),
            node->receiverMode);
    }

    auto *llvmFunc = llvm::Function::Create(
        getFunctionAbiLLVMType(*typeMgr, funcType, methodParent != nullptr),
        llvm::Function::ExternalLinkage, llvm::Twine(llvmName),
        typeMgr->getModule());
    annotateFunctionAbi(*llvmFunc, funcType->getAbiKind());
    auto *func = new Function(
        llvmFunc, funcType, extractParamNames(node),
        methodParent ? std::optional<ReceiverMode>(node->receiverMode)
                     : std::nullopt);

    if (methodParent) {
        typeMgr->bindMethodFunction(
            methodParent, llvm::StringRef(funcName.tochara(), funcName.size()),
            func);
    } else {
        scope.addObj(llvm::StringRef(llvmName), func);
    }
    return func;
}

Function *
declareExtensionFunction(Scope &scope, TypeTable *typeMgr,
                         AstExtendDecl *extendDecl, AstFuncDecl *node,
                         CompilationUnit *unit, bool exportNamespace) {
    if (!extendDecl || !node) {
        return nullptr;
    }
    if (node->isExternC()) {
        error(node->loc,
              "#[extern \"C\"] is not supported on extension methods",
              "Declare a top-level wrapper function instead.");
    }
    if (node->hasTypeParams()) {
        return nullptr;
    }

    validateExtensionTargetShape(extendDecl);

    auto *targetType = resolveTypeNode(typeMgr, unit, extendDecl->targetType);
    if (!targetType) {
        error(extendDecl->loc,
              "unknown extend target type: " +
                  describeTypeNode(extendDecl->targetType, "void"),
              "Declare or import the target type before this `extend` block.");
    }
    if (!isBuiltinScalarExtensionBase(targetType) &&
        !asUnqualified<StructType>(targetType)) {
        error(extendDecl->loc,
              "unsupported extend target `" +
                  describeTypeNode(extendDecl->targetType, "void") + "`",
              "`extend` currently supports concrete structs and builtin "
              "scalar types.");
    }
    if (node->receiverMode == ReceiverMode::Value) {
        if (auto *structType = asUnqualified<StructType>(targetType);
            structType && structType->isOpaqueDecl()) {
            error(node->loc,
                  "`var def` requires a complete receiver type, got opaque `" +
                      toStdString(structType->full_name) + "`",
                  "Use a borrowed extension method or complete the struct "
                  "layout.");
        }
    }

    const auto targetTypeSpelling = toStdString(targetType->full_name);
    auto resolvedFunctionName = resolveExtensionMethodSymbolName(
        unit, targetTypeSpelling, toStringRef(node->name), node->receiverMode,
        exportNamespace);

    Function *existingFunction = nullptr;
    if (auto *existing = scope.getObj(llvm::StringRef(resolvedFunctionName))) {
        existingFunction = existing->as<Function>();
        if (!existingFunction) {
            error(node->loc, "extension method `" + resolvedFunctionName +
                                 "` conflicts with an existing symbol");
        }
    }

    std::vector<TypeClass *> argTypes{
        extensionReceiverType(typeMgr, targetType, node->receiverMode)};
    auto argBindingKinds = extractParamBindingKinds(node, 0, true);
    TypeClass *retType = nullptr;
    if (node->retType) {
        retType =
            resolveContextualSelfType(typeMgr, unit, node->retType, targetType);
        if (!retType) {
            error(node->loc, "unknown return type for extension method `" +
                                 toStdString(node->name) + "`: " +
                                 describeTypeNode(node->retType, "void"));
        }
        rejectBareFunctionType(
            retType, node->retType,
            "unsupported bare function return type for extension method `" +
                toStdString(node->name) + "`",
            node->loc);
        rejectOpaqueStructByValue(retType, node->retType, node->loc,
                                  "return type of extension method `" +
                                      toStdString(node->name) + "`");
    }

    if (node->args) {
        for (auto *arg : *node->args) {
            if (!arg->is<AstVarDecl>()) {
                error(node->loc,
                      "invalid extension method parameter declaration in `" +
                          toStdString(node->name) + "`");
            }
            auto *varDecl = arg->as<AstVarDecl>();
            auto *type = resolveContextualSelfType(
                typeMgr, unit, varDecl->typeNode, targetType);
            if (!type) {
                error(varDecl->loc,
                      "unknown type for extension method parameter `" +
                          toStdString(varDecl->field) + "` in `" +
                          toStdString(node->name) +
                          "`: " + describeTypeNode(varDecl->typeNode, "void"));
            }
            rejectBareFunctionType(
                type, varDecl->typeNode,
                "unsupported bare function parameter type for `" +
                    toStdString(varDecl->field) + "` in extension method `" +
                    toStdString(node->name) + "`",
                varDecl->loc);
            rejectOpaqueStructByValue(
                type, varDecl->typeNode, varDecl->loc,
                "parameter `" + toStdString(varDecl->field) +
                    "` in extension method `" + toStdString(node->name) + "`");
            argTypes.push_back(type);
        }
    }

    auto *funcType = typeMgr->getOrCreateFunctionType(
        argTypes, retType, std::move(argBindingKinds), node->abiKind);
    if (existingFunction) {
        auto *existingType = existingFunction->getType()->as<FuncType>();
        if (existingType != funcType) {
            reportLocalFunctionConflict(node, resolvedFunctionName,
                                        existingType, funcType);
        }
        return existingFunction;
    }

    auto *llvmFunc = llvm::Function::Create(
        getFunctionAbiLLVMType(*typeMgr, funcType, true),
        llvm::Function::ExternalLinkage, llvm::Twine(resolvedFunctionName),
        typeMgr->getModule());
    annotateFunctionAbi(*llvmFunc, funcType->getAbiKind());
    auto *func = new Function(llvmFunc, funcType, extractParamNames(node),
                              node->receiverMode);
    scope.addObj(llvm::StringRef(resolvedFunctionName), func);
    return func;
}

}  // namespace declarationsupport_impl
}  // namespace lona
