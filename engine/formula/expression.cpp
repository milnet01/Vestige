/// @file expression.cpp
/// @brief Expression tree AST implementation.
#include "formula/expression.h"

#include <algorithm>
#include <stdexcept>

namespace Vestige
{

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
    auto node = std::make_unique<ExprNode>();
    node->type = ExprNodeType::VARIABLE;
    node->name = varName;
    return node;
}

std::unique_ptr<ExprNode> ExprNode::binaryOp(const std::string& opName,
                                              std::unique_ptr<ExprNode> left,
                                              std::unique_ptr<ExprNode> right)
{
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
    switch (type)
    {
    case ExprNodeType::LITERAL:
        return value;

    case ExprNodeType::VARIABLE:
        return {{"var", name}};

    case ExprNodeType::BINARY_OP:
        return {
            {"op", op},
            {"left", children[0]->toJson()},
            {"right", children[1]->toJson()}
        };

    case ExprNodeType::UNARY_OP:
        return {
            {"fn", op},
            {"arg", children[0]->toJson()}
        };

    case ExprNodeType::CONDITIONAL:
        return {
            {"if", children[0]->toJson()},
            {"then", children[1]->toJson()},
            {"else", children[2]->toJson()}
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
