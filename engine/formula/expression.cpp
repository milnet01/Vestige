// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file expression.cpp
/// @brief Expression tree AST implementation.
#include "formula/expression.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Identifier and operator allowlists (AUDIT.md §H11, FIXPLAN E3)
// ---------------------------------------------------------------------------
//
// ExprNode.name (VARIABLE) and .op (BINARY_OP/UNARY_OP) flow through
// codegen_cpp/codegen_glsl as raw string-splices into generated C++ and
// GLSL source. An unvalidated name or op is a codegen-injection vector:
// a preset JSON like {"var": "x); system(\"rm -rf /\"); float y("} would
// splice shell invocations into the generated header.
//
// Load-time validation means the in-memory tree is guaranteed clean
// before any codegen runs, and codegen can trust its inputs.

namespace
{

// Strict C identifier: [A-Za-z_][A-Za-z0-9_]*; bounded length matches the
// longest legitimate formula variable name we expect (128 is ample).
bool isValidIdentifier(const std::string& s)
{
    if (s.empty() || s.size() > 128) return false;
    unsigned char c0 = static_cast<unsigned char>(s[0]);
    if (!(std::isalpha(c0) || c0 == '_')) return false;
    for (size_t i = 1; i < s.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

// Allowed binary ops — mirrors codegen_cpp/codegen_glsl switch arms.
const std::unordered_set<std::string>& allowedBinaryOps()
{
    static const std::unordered_set<std::string> s = {
        "+", "-", "*", "/", "pow", "min", "max", "mod", "dot"
    };
    return s;
}

// Allowed unary ops — mirrors codegen_cpp/codegen_glsl switch arms.
const std::unordered_set<std::string>& allowedUnaryOps()
{
    static const std::unordered_set<std::string> s = {
        "sin", "cos", "sqrt", "abs", "exp", "log", "floor", "ceil",
        "negate", "saturate", "sign"
    };
    return s;
}

} // namespace

bool ExprNode::isValidVariableName(const std::string& identName)
{
    return isValidIdentifier(identName);
}

bool ExprNode::isAllowedBinaryOp(const std::string& opName)
{
    return allowedBinaryOps().count(opName) > 0;
}

bool ExprNode::isAllowedUnaryOp(const std::string& opName)
{
    return allowedUnaryOps().count(opName) > 0;
}

// ---------------------------------------------------------------------------
// Factory functions
// ---------------------------------------------------------------------------

std::unique_ptr<ExprNode> ExprNode::literal(float val)
{
    auto node = std::make_unique<ExprNode>();
    node->type = ExprNodeType::LITERAL;
    node->value = val;
    return node;
}

std::unique_ptr<ExprNode> ExprNode::variable(const std::string& varName)
{
    if (!isValidIdentifier(varName))
    {
        throw std::runtime_error(
            "ExprNode::variable: invalid identifier '" + varName +
            "' (must match [A-Za-z_][A-Za-z0-9_]*, <=128 chars) "
            "— see AUDIT.md §H11");
    }
    auto node = std::make_unique<ExprNode>();
    node->type = ExprNodeType::VARIABLE;
    node->name = varName;
    return node;
}

std::unique_ptr<ExprNode> ExprNode::binaryOp(const std::string& opName,
                                              std::unique_ptr<ExprNode> left,
                                              std::unique_ptr<ExprNode> right)
{
    if (!isAllowedBinaryOp(opName))
    {
        throw std::runtime_error(
            "ExprNode::binaryOp: operator '" + opName + "' is not in the "
            "allowlist (+, -, *, /, pow, min, max, mod, dot) "
            "— see AUDIT.md §H11");
    }
    auto node = std::make_unique<ExprNode>();
    node->type = ExprNodeType::BINARY_OP;
    node->op = opName;
    node->children.push_back(std::move(left));
    node->children.push_back(std::move(right));
    return node;
}

std::unique_ptr<ExprNode> ExprNode::unaryOp(const std::string& fnName,
                                             std::unique_ptr<ExprNode> arg)
{
    if (!isAllowedUnaryOp(fnName))
    {
        throw std::runtime_error(
            "ExprNode::unaryOp: function '" + fnName + "' is not in the "
            "allowlist (sin, cos, sqrt, abs, exp, log, floor, ceil, "
            "negate, saturate, sign) — see AUDIT.md §H11");
    }
    auto node = std::make_unique<ExprNode>();
    node->type = ExprNodeType::UNARY_OP;
    node->op = fnName;
    node->children.push_back(std::move(arg));
    return node;
}

std::unique_ptr<ExprNode> ExprNode::conditional(std::unique_ptr<ExprNode> condition,
                                                 std::unique_ptr<ExprNode> trueExpr,
                                                 std::unique_ptr<ExprNode> falseExpr)
{
    auto node = std::make_unique<ExprNode>();
    node->type = ExprNodeType::CONDITIONAL;
    node->children.push_back(std::move(condition));
    node->children.push_back(std::move(trueExpr));
    node->children.push_back(std::move(falseExpr));
    return node;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

std::unique_ptr<ExprNode> ExprNode::clone() const
{
    auto copy = std::make_unique<ExprNode>();
    copy->type = type;
    copy->value = value;
    copy->name = name;
    copy->op = op;
    for (const auto& child : children)
    {
        copy->children.push_back(child ? child->clone() : nullptr);
    }
    return copy;
}

bool ExprNode::usesVariable(const std::string& varName) const
{
    if (type == ExprNodeType::VARIABLE && name == varName)
    {
        return true;
    }
    for (const auto& child : children)
    {
        if (child && child->usesVariable(varName))
        {
            return true;
        }
    }
    return false;
}

void ExprNode::collectVariables(std::vector<std::string>& out) const
{
    if (type == ExprNodeType::VARIABLE)
    {
        if (std::find(out.begin(), out.end(), name) == out.end())
        {
            out.push_back(name);
        }
    }
    for (const auto& child : children)
    {
        if (child)
        {
            child->collectVariables(out);
        }
    }
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

nlohmann::json ExprNode::toJson() const
{
    // Guard against malformed in-memory trees (AUDIT.md §M12). A crash on
    // save is a worse outcome than losing a bad sub-tree: emit a null
    // placeholder so the error surfaces via a round-trip rather than
    // dereferencing a null child.
    auto childJson = [this](size_t i) -> nlohmann::json
    {
        return (i < children.size() && children[i])
               ? children[i]->toJson()
               : nlohmann::json(nullptr);
    };

    switch (type)
    {
    case ExprNodeType::LITERAL:
        return value;

    case ExprNodeType::VARIABLE:
        return {{"var", name}};

    case ExprNodeType::BINARY_OP:
        return {
            {"op", op},
            {"left", childJson(0)},
            {"right", childJson(1)}
        };

    case ExprNodeType::UNARY_OP:
        return {
            {"fn", op},
            {"arg", childJson(0)}
        };

    case ExprNodeType::CONDITIONAL:
        return {
            {"if", childJson(0)},
            {"then", childJson(1)},
            {"else", childJson(2)}
        };
    }

    return nullptr;
}

std::unique_ptr<ExprNode> ExprNode::fromJson(const nlohmann::json& j)
{
    // Bare number → LITERAL
    if (j.is_number())
    {
        return literal(j.get<float>());
    }

    // Must be an object from here
    if (!j.is_object())
    {
        throw std::runtime_error("ExprNode::fromJson: expected number or object");
    }

    // {"var": "name"} → VARIABLE
    if (j.contains("var"))
    {
        return variable(j["var"].get<std::string>());
    }

    // {"op": ..., "left": ..., "right": ...} → BINARY_OP
    if (j.contains("op") && j.contains("left") && j.contains("right"))
    {
        return binaryOp(
            j["op"].get<std::string>(),
            fromJson(j["left"]),
            fromJson(j["right"])
        );
    }

    // {"fn": ..., "arg": ...} → UNARY_OP
    if (j.contains("fn") && j.contains("arg"))
    {
        return unaryOp(
            j["fn"].get<std::string>(),
            fromJson(j["arg"])
        );
    }

    // {"if": ..., "then": ..., "else": ...} → CONDITIONAL
    if (j.contains("if") && j.contains("then") && j.contains("else"))
    {
        return conditional(
            fromJson(j["if"]),
            fromJson(j["then"]),
            fromJson(j["else"])
        );
    }

    throw std::runtime_error("ExprNode::fromJson: unrecognized node format");
}

} // namespace Vestige
