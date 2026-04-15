// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file expression.h
/// @brief Expression tree AST for the Formula Pipeline.
///
/// Formulas are stored as expression trees during development and compiled
/// to native C++ or GLSL at build time. The tree supports JSON round-trip
/// serialization for the formula library.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Vestige
{

/// @brief Node types in the expression tree.
enum class ExprNodeType
{
    LITERAL,      ///< Constant float value (no children)
    VARIABLE,     ///< Named input variable or coefficient (no children)
    BINARY_OP,    ///< Infix operator: +, -, *, /, pow, min, max, dot (2 children)
    UNARY_OP,     ///< Function call: sin, cos, sqrt, abs, exp, log, floor, ceil,
                  ///< negate, saturate (1 child)
    CONDITIONAL   ///< Ternary: condition ? trueExpr : falseExpr (3 children)
};

/// @brief A single node in the expression tree.
///
/// Nodes form a tree via unique_ptr children. The tree is immutable once built —
/// modifications create new trees via clone().
struct ExprNode
{
    ExprNodeType type;
    float value = 0.0f;        ///< For LITERAL nodes
    std::string name;          ///< For VARIABLE nodes (variable name)
    std::string op;            ///< For BINARY_OP / UNARY_OP (operator or function name)
    std::vector<std::unique_ptr<ExprNode>> children;

    // -- Factory functions --------------------------------------------------

    /// @brief Creates a literal constant node.
    static std::unique_ptr<ExprNode> literal(float val);

    /// @brief Creates a variable reference node.
    static std::unique_ptr<ExprNode> variable(const std::string& varName);

    /// @brief Creates a binary operator node (+, -, *, /, pow, min, max, dot).
    static std::unique_ptr<ExprNode> binaryOp(const std::string& opName,
                                               std::unique_ptr<ExprNode> left,
                                               std::unique_ptr<ExprNode> right);

    /// @brief Creates a unary function node (sin, cos, sqrt, abs, exp, log,
    ///        floor, ceil, negate, saturate).
    static std::unique_ptr<ExprNode> unaryOp(const std::string& fnName,
                                              std::unique_ptr<ExprNode> arg);

    /// @brief Creates a conditional (ternary) node.
    static std::unique_ptr<ExprNode> conditional(std::unique_ptr<ExprNode> condition,
                                                  std::unique_ptr<ExprNode> trueExpr,
                                                  std::unique_ptr<ExprNode> falseExpr);

    // -- Utilities ----------------------------------------------------------

    /// @brief Deep-copies this node and all children.
    std::unique_ptr<ExprNode> clone() const;

    /// @brief Returns true if this subtree references the named variable.
    bool usesVariable(const std::string& varName) const;

    /// @brief Collects all variable names referenced in this subtree.
    void collectVariables(std::vector<std::string>& out) const;

    // -- JSON serialization -------------------------------------------------
    // Format:
    //   LITERAL:     bare number (e.g. 3.14)
    //   VARIABLE:    {"var": "name"}
    //   BINARY_OP:   {"op": "+", "left": ..., "right": ...}
    //   UNARY_OP:    {"fn": "sin", "arg": ...}
    //   CONDITIONAL: {"if": ..., "then": ..., "else": ...}

    /// @brief Serializes this node to JSON.
    nlohmann::json toJson() const;

    /// @brief Deserializes an expression tree from JSON.
    ///
    /// Validates variable names against [A-Za-z_][A-Za-z0-9_]* and operators
    /// against an allowlist, per AUDIT.md §H11 / FIXPLAN E3 (codegen-injection
    /// hardening). Throws std::runtime_error on unrecognised format,
    /// disallowed operator, or invalid identifier.
    static std::unique_ptr<ExprNode> fromJson(const nlohmann::json& j);

    // -- Validation helpers (AUDIT.md §H11, FIXPLAN E3) ---------------------
    //
    // These are the single source of truth used by factory functions and
    // fromJson so that every path building an ExprNode rejects hostile
    // inputs before they reach codegen.

    /// @brief True if @p name is a valid C-style identifier, <=128 chars.
    static bool isValidVariableName(const std::string& name);

    /// @brief True if @p op is one of the binary ops codegen understands.
    ///        Mirrors the switch arms in codegen_cpp/codegen_glsl.
    static bool isAllowedBinaryOp(const std::string& op);

    /// @brief True if @p op is one of the unary ops codegen understands.
    static bool isAllowedUnaryOp(const std::string& op);
};

} // namespace Vestige
