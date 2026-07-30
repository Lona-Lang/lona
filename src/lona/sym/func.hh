#pragma once

#include "lona/ast/astnode.hh"
#include "object.hh"
#include <optional>
#include <string>
#include <vector>

namespace lona {

class Function : public Object {
    std::vector<string> paramNames_;
    std::optional<ReceiverMode> receiverMode_;

public:
    Function(llvm::Function *val, FuncType *type,
             std::vector<string> paramNames = {},
             std::optional<ReceiverMode> receiverMode = std::nullopt)
        : Object((llvm::Function *)val, (TypeClass *)type),
          paramNames_(std::move(paramNames)),
          receiverMode_(receiverMode) {}

    ObjectPtr call(Scope *scope, const std::vector<ObjectPtr> &args);
    const std::vector<string> &paramNames() const { return paramNames_; }
    bool hasImplicitSelf() const { return receiverMode_.has_value(); }
    std::optional<ReceiverMode> receiverMode() const { return receiverMode_; }

    llvm::Value *get(Scope *scope) override { return val; }

    void set(Scope *scope, Object *src) override {
        throw "readonly literal value";
    }
};

ObjectPtr
emitFunctionCall(Scope *scope, llvm::Value *calleeValue, FuncType *funcType,
                 const std::vector<ObjectPtr> &args,
                 bool hasImplicitSelf = false);

}
