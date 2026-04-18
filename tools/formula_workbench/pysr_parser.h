// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file pysr_parser.h
/// @brief Parser for PySR-format expression strings → ExprNode.
///
/// PySR emits equations as short algebraic strings — e.g. ``cos(x0) * x1``,
/// ``(x0 - 1.5)**2 + 0.3``, ``sqrt(x0^2 + x1^2)``. The "Import as library
/// formula" action in the Workbench needs to turn those strings back into
/// an ExprNode so the fit can be registered in the FormulaLibrary.
///
/// Grammar (precedence climbing, standard math precedence):
/// @code
///   expr     ::= addExpr
///   addExpr  ::= mulExpr ( ('+' | '-') mulExpr )*
///   mulExpr  ::= unary   ( ('*' | '/') unary   )*
///   unary    ::= '-' unary | '+' unary | powExpr
///   powExpr  ::= primary ( ('^' | '**') unary )?            // right-assoc
///   primary  ::= NUMBER
///              | IDENT '(' expr ')'                         // unary call
///              | IDENT
///              | '(' expr ')'
/// @endcode
///
/// Supported unary functions map 1:1 to ``ExprNode::unaryOp``: ``cos``,
/// ``sin``, ``exp``, ``log``, ``sqrt``, ``abs``, ``floor``, ``ceil``. Binary
/// ops: ``+``, ``-``, ``*``, ``/``, and ``^`` / ``**`` → ``pow``. Unary
/// minus maps to ``negate``; unary plus is a no-op.
///
/// Everything funnels through the ``ExprNode::variable`` / ``binaryOp`` /
/// ``unaryOp`` factories, so the H11 allowlist is enforced on every node
/// the parser constructs — a hostile equation string cannot bypass it.
#pragma once

#include "formula/expression.h"

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{
namespace pysr
{

/// @brief Result of parsing a PySR expression string.
///
/// On success ``tree`` holds the parsed expression and ``error`` is empty.
/// On failure ``tree`` is null and ``error`` contains a human-readable
/// message (including token-position context where possible).
struct ParseResult
{
    std::unique_ptr<ExprNode> tree;   ///< Parsed tree, or null on failure.
    std::vector<std::string>  variables;  ///< Distinct variable names referenced.
    std::string               error;
};

/// @brief Parses a PySR equation string into an ExprNode.
///
/// @param expression The PySR equation string (e.g. ``cos(x0) * x1 + 0.5``).
/// @return ParseResult — on success, ``tree`` is populated and ``error`` is
///         empty. On failure, ``tree`` is null and ``error`` describes the
///         parse failure.
ParseResult parseExpression(const std::string& expression);

/// @brief Returns true if @p functionName is one of the unary functions
///        the parser recognises. Useful for UI validation messages.
bool isRecognisedFunction(const std::string& functionName);

} // namespace pysr
} // namespace Vestige
