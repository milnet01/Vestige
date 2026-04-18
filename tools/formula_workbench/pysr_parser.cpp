// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file pysr_parser.cpp
/// @brief PySR expression string → ExprNode parser.
#include "pysr_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <unordered_set>

namespace Vestige
{
namespace pysr
{

namespace
{

// Unary function names the parser accepts. Intentionally a subset of
// ``isAllowedUnaryOp`` (the codegen allowlist) — anything beyond this
// set is either never emitted by PySR defaults (``saturate``, ``sign``)
// or maps via a different node type (``negate`` via unary minus).
const std::unordered_set<std::string>& recognisedFunctions()
{
    static const std::unordered_set<std::string> s = {
        "cos", "sin", "exp", "log", "sqrt", "abs", "floor", "ceil"
    };
    return s;
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

enum class TokenType
{
    Number,
    Ident,
    Plus,
    Minus,
    Star,
    Slash,
    Caret,        ///< ``^``
    StarStar,     ///< ``**``
    LParen,
    RParen,
    End
};

struct Token
{
    TokenType   type = TokenType::End;
    std::string text;
    size_t      pos = 0;   ///< Byte offset in the source for error messages.
};

class Tokenizer
{
public:
    explicit Tokenizer(const std::string& src) : m_src(src) {}

    Token next()
    {
        skipWhitespace();
        if (m_pos >= m_src.size())
            return {TokenType::End, "", m_pos};

        const size_t start = m_pos;
        const char   c     = m_src[m_pos];

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
            return readNumber(start);
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            return readIdent(start);

        switch (c)
        {
        case '+': ++m_pos; return {TokenType::Plus,   "+", start};
        case '-': ++m_pos; return {TokenType::Minus,  "-", start};
        case '*':
            if (m_pos + 1 < m_src.size() && m_src[m_pos + 1] == '*')
            {
                m_pos += 2;
                return {TokenType::StarStar, "**", start};
            }
            ++m_pos;
            return {TokenType::Star, "*", start};
        case '/': ++m_pos; return {TokenType::Slash,  "/", start};
        case '^': ++m_pos; return {TokenType::Caret,  "^", start};
        case '(': ++m_pos; return {TokenType::LParen, "(", start};
        case ')': ++m_pos; return {TokenType::RParen, ")", start};
        default:  break;
        }

        // Unknown character — surface it with its position so the
        // caller can point at it in the error.
        std::string bad(1, c);
        throw std::runtime_error(
            "unexpected character '" + bad + "' at position "
            + std::to_string(start));
    }

private:
    void skipWhitespace()
    {
        while (m_pos < m_src.size()
               && std::isspace(static_cast<unsigned char>(m_src[m_pos])))
            ++m_pos;
    }

    Token readNumber(size_t start)
    {
        // Float: [0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?  — also accepts .5
        bool sawDot = false;
        while (m_pos < m_src.size())
        {
            const char c = m_src[m_pos];
            if (std::isdigit(static_cast<unsigned char>(c)))
            {
                ++m_pos;
            }
            else if (c == '.' && !sawDot)
            {
                sawDot = true;
                ++m_pos;
            }
            else break;
        }
        // Optional exponent.
        if (m_pos < m_src.size() && (m_src[m_pos] == 'e' || m_src[m_pos] == 'E'))
        {
            ++m_pos;
            if (m_pos < m_src.size()
                && (m_src[m_pos] == '+' || m_src[m_pos] == '-'))
                ++m_pos;
            while (m_pos < m_src.size()
                   && std::isdigit(static_cast<unsigned char>(m_src[m_pos])))
                ++m_pos;
        }
        return {TokenType::Number, m_src.substr(start, m_pos - start), start};
    }

    Token readIdent(size_t start)
    {
        while (m_pos < m_src.size())
        {
            const char c = m_src[m_pos];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                ++m_pos;
            else
                break;
        }
        return {TokenType::Ident, m_src.substr(start, m_pos - start), start};
    }

    const std::string& m_src;
    size_t             m_pos = 0;
};

// ---------------------------------------------------------------------------
// Parser (precedence climbing)
// ---------------------------------------------------------------------------

class Parser
{
public:
    explicit Parser(const std::string& src)
        : m_tokenizer(src)
    {
        advance();
    }

    std::unique_ptr<ExprNode> parseExpr()
    {
        auto tree = parseAdd();
        if (m_current.type != TokenType::End)
        {
            throw std::runtime_error(
                "unexpected token '" + m_current.text + "' at position "
                + std::to_string(m_current.pos));
        }
        return tree;
    }

    std::vector<std::string> takeVariables()
    {
        return std::move(m_variables);
    }

private:
    // addExpr ::= mulExpr ( ('+' | '-') mulExpr )*
    std::unique_ptr<ExprNode> parseAdd()
    {
        auto left = parseMul();
        while (m_current.type == TokenType::Plus
               || m_current.type == TokenType::Minus)
        {
            const std::string op = m_current.text;
            advance();
            auto right = parseMul();
            left = ExprNode::binaryOp(op, std::move(left), std::move(right));
        }
        return left;
    }

    // mulExpr ::= unary ( ('*' | '/') unary )*
    std::unique_ptr<ExprNode> parseMul()
    {
        auto left = parseUnary();
        while (m_current.type == TokenType::Star
               || m_current.type == TokenType::Slash)
        {
            const std::string op = m_current.text;
            advance();
            auto right = parseUnary();
            left = ExprNode::binaryOp(op, std::move(left), std::move(right));
        }
        return left;
    }

    // unary ::= '-' unary | '+' unary | powExpr
    std::unique_ptr<ExprNode> parseUnary()
    {
        if (m_current.type == TokenType::Minus)
        {
            advance();
            auto inner = parseUnary();
            return ExprNode::unaryOp("negate", std::move(inner));
        }
        if (m_current.type == TokenType::Plus)
        {
            advance();
            return parseUnary();
        }
        return parsePow();
    }

    // powExpr ::= primary ( ('^' | '**') unary )?    right-associative
    std::unique_ptr<ExprNode> parsePow()
    {
        auto base = parsePrimary();
        if (m_current.type == TokenType::Caret
            || m_current.type == TokenType::StarStar)
        {
            advance();
            // Right-associative: 2^3^4 = 2^(3^4). parseUnary lets the
            // RHS itself be a unary-negated power.
            auto exponent = parseUnary();
            base = ExprNode::binaryOp("pow", std::move(base),
                                      std::move(exponent));
        }
        return base;
    }

    // primary ::= NUMBER | IDENT '(' expr ')' | IDENT | '(' expr ')'
    std::unique_ptr<ExprNode> parsePrimary()
    {
        if (m_current.type == TokenType::Number)
        {
            // std::strtof is locale-aware; PySR emits '.' as decimal
            // separator regardless of locale, so use it carefully.
            // setlocale changes are global, so rely on the canonical
            // "C" locale being default for the tool.
            const std::string text = m_current.text;
            advance();
            const float v = std::strtof(text.c_str(), nullptr);
            return ExprNode::literal(v);
        }

        if (m_current.type == TokenType::Ident)
        {
            const std::string name = m_current.text;
            const size_t      pos  = m_current.pos;
            advance();

            if (m_current.type == TokenType::LParen)
            {
                // Function call.
                if (!isRecognisedFunction(name))
                {
                    throw std::runtime_error(
                        "unknown function '" + name + "' at position "
                        + std::to_string(pos)
                        + " (supported: cos, sin, exp, log, sqrt, abs, "
                          "floor, ceil)");
                }
                advance();   // consume '('
                auto arg = parseAdd();
                if (m_current.type != TokenType::RParen)
                {
                    throw std::runtime_error(
                        "expected ')' after argument of '" + name
                        + "' at position "
                        + std::to_string(m_current.pos));
                }
                advance();   // consume ')'
                return ExprNode::unaryOp(name, std::move(arg));
            }

            // Plain variable reference. Record it for the UI to pick up.
            if (std::find(m_variables.begin(), m_variables.end(), name)
                == m_variables.end())
                m_variables.push_back(name);
            return ExprNode::variable(name);
        }

        if (m_current.type == TokenType::LParen)
        {
            advance();
            auto inner = parseAdd();
            if (m_current.type != TokenType::RParen)
            {
                throw std::runtime_error(
                    "expected ')' at position "
                    + std::to_string(m_current.pos));
            }
            advance();
            return inner;
        }

        if (m_current.type == TokenType::End)
        {
            throw std::runtime_error("unexpected end of expression");
        }
        throw std::runtime_error(
            "unexpected token '" + m_current.text + "' at position "
            + std::to_string(m_current.pos));
    }

    void advance()
    {
        m_current = m_tokenizer.next();
    }

    Tokenizer                m_tokenizer;
    Token                    m_current;
    std::vector<std::string> m_variables;
};

} // namespace

bool isRecognisedFunction(const std::string& functionName)
{
    return recognisedFunctions().count(functionName) > 0;
}

ParseResult parseExpression(const std::string& expression)
{
    ParseResult result;
    try
    {
        Parser parser(expression);
        result.tree      = parser.parseExpr();
        result.variables = parser.takeVariables();
    }
    catch (const std::exception& e)
    {
        result.tree.reset();
        result.variables.clear();
        result.error = e.what();
    }
    return result;
}

} // namespace pysr
} // namespace Vestige
