/// @file expression_eval.cpp
/// @brief Tree-walking expression evaluator implementation.
#include "formula/expression_eval.h"
#include "formula/formula.h"
#include "formula/safe_math.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Internal evaluation with merged variable/coefficient lookup
// ---------------------------------------------------------------------------

static float evalNode(const ExprNode& node,
                      const ExpressionEvaluator::VariableMap& vars,
                      const std::unordered_map<std::string, float>& coeffs)
{
    switch (node.type)
    {
    case ExprNodeType::LITERAL:
        return node.value;

    case ExprNodeType::VARIABLE:
    {
        // Check variables first, then coefficients
        auto it = vars.find(node.name);
        if (it != vars.end())
        {
            return it->second;
        }
        auto cit = coeffs.find(node.name);
        if (cit != coeffs.end())
        {
            return cit->second;
        }
        throw std::runtime_error("Undefined variable: " + node.name);
    }

    case ExprNodeType::BINARY_OP:
    {
        if (node.children.size() < 2 || !node.children[0] || !node.children[1])
        {
            throw std::runtime_error("BINARY_OP requires 2 children");
        }
        float left = evalNode(*node.children[0], vars, coeffs);
        float right = evalNode(*node.children[1], vars, coeffs);

        if (node.op == "+")   return left + right;
        if (node.op == "-")   return left - right;
        if (node.op == "*")   return left * right;
        if (node.op == "/")   return SafeMath::safeDiv(left, right);
        if (node.op == "pow") return std::pow(left, right);
        if (node.op == "min") return std::fmin(left, right);
        if (node.op == "max") return std::fmax(left, right);
        if (node.op == "mod") return std::fmod(left, right);

        throw std::runtime_error("Unknown binary op: " + node.op);
    }

    case ExprNodeType::UNARY_OP:
    {
        if (node.children.empty() || !node.children[0])
        {
            throw std::runtime_error("UNARY_OP requires 1 child");
        }
        float arg = evalNode(*node.children[0], vars, coeffs);

        if (node.op == "sin")      return std::sin(arg);
        if (node.op == "cos")      return std::cos(arg);
        if (node.op == "sqrt")     return SafeMath::safeSqrt(arg);
        if (node.op == "abs")      return std::fabs(arg);
        if (node.op == "exp")      return std::exp(arg);
        if (node.op == "log")      return SafeMath::safeLog(arg);
        if (node.op == "floor")    return std::floor(arg);
        if (node.op == "ceil")     return std::ceil(arg);
        if (node.op == "negate")   return -arg;
        if (node.op == "saturate") return std::fmin(1.0f, std::fmax(0.0f, arg));
        if (node.op == "sign")
        {
            if (arg > 0.0f) return 1.0f;
            if (arg < 0.0f) return -1.0f;
            return 0.0f;
        }

        throw std::runtime_error("Unknown unary op: " + node.op);
    }

    case ExprNodeType::CONDITIONAL:
    {
        if (node.children.size() < 3 || !node.children[0] ||
            !node.children[1] || !node.children[2])
        {
            throw std::runtime_error("CONDITIONAL requires 3 children");
        }
        float cond = evalNode(*node.children[0], vars, coeffs);
        // Non-zero = true (like C)
        if (cond != 0.0f)
        {
            return evalNode(*node.children[1], vars, coeffs);
        }
        return evalNode(*node.children[2], vars, coeffs);
    }
    }

    throw std::runtime_error("Unknown node type");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

float ExpressionEvaluator::evaluate(const ExprNode& node,
                                     const VariableMap& variables) const
{
    static const std::unordered_map<std::string, float> emptyCoeffs;
    return evalNode(node, variables, emptyCoeffs);
}

float ExpressionEvaluator::evaluate(const ExprNode& node,
                                     const VariableMap& variables,
                                     const std::unordered_map<std::string, float>& coefficients) const
{
    return evalNode(node, variables, coefficients);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

bool ExpressionEvaluator::validate(const ExprNode& node,
                                    const std::vector<FormulaInput>& inputs,
                                    const std::vector<std::string>& coefficients,
                                    std::string& errorOut)
{
    std::vector<std::string> usedVars;
    node.collectVariables(usedVars);

    for (const auto& varName : usedVars)
    {
        // Check inputs
        bool found = false;
        for (const auto& inp : inputs)
        {
            if (inp.name == varName)
            {
                found = true;
                break;
            }
        }

        // Check coefficients
        if (!found)
        {
            found = std::find(coefficients.begin(), coefficients.end(), varName)
                    != coefficients.end();
        }

        if (!found)
        {
            errorOut = "Undeclared variable: " + varName;
            return false;
        }
    }

    return true;
}

} // namespace Vestige
