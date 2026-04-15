// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file formula_library.cpp
/// @brief FormulaLibrary implementation.
#include "formula/formula_library.h"
#include "formula/physics_templates.h"
#include "core/logger.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace Vestige
{

void FormulaLibrary::registerFormula(FormulaDefinition def)
{
    std::string name = def.name;
    m_formulas[name] = std::move(def);
}

bool FormulaLibrary::removeFormula(const std::string& name)
{
    return m_formulas.erase(name) > 0;
}

const FormulaDefinition* FormulaLibrary::findByName(const std::string& name) const
{
    auto it = m_formulas.find(name);
    if (it != m_formulas.end())
    {
        return &it->second;
    }
    return nullptr;
}

std::vector<const FormulaDefinition*> FormulaLibrary::findByCategory(
    const std::string& category) const
{
    std::vector<const FormulaDefinition*> result;
    for (const auto& [name, def] : m_formulas)
    {
        if (def.category == category)
        {
            result.push_back(&def);
        }
    }
    return result;
}

std::vector<std::string> FormulaLibrary::getCategories() const
{
    std::vector<std::string> cats;
    for (const auto& [name, def] : m_formulas)
    {
        if (std::find(cats.begin(), cats.end(), def.category) == cats.end())
        {
            cats.push_back(def.category);
        }
    }
    std::sort(cats.begin(), cats.end());
    return cats;
}

std::vector<const FormulaDefinition*> FormulaLibrary::getAll() const
{
    std::vector<const FormulaDefinition*> result;
    result.reserve(m_formulas.size());
    for (const auto& [name, def] : m_formulas)
    {
        result.push_back(&def);
    }
    return result;
}

size_t FormulaLibrary::count() const
{
    return m_formulas.size();
}

void FormulaLibrary::clear()
{
    m_formulas.clear();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

size_t FormulaLibrary::loadFromJson(const nlohmann::json& j)
{
    size_t loaded = 0;

    if (j.is_array())
    {
        for (const auto& item : j)
        {
            try
            {
                auto def = FormulaDefinition::fromJson(item);
                if (!def.name.empty())
                {
                    registerFormula(std::move(def));
                    ++loaded;
                }
            }
            catch (const std::exception& e)
            {
                Logger::warning(std::string("FormulaLibrary: failed to load formula: ") + e.what());
            }
        }
    }
    else if (j.is_object())
    {
        // Single formula
        try
        {
            auto def = FormulaDefinition::fromJson(j);
            if (!def.name.empty())
            {
                registerFormula(std::move(def));
                ++loaded;
            }
        }
        catch (const std::exception& e)
        {
            Logger::warning(std::string("FormulaLibrary: failed to load formula: ") + e.what());
        }
    }

    return loaded;
}

size_t FormulaLibrary::loadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        Logger::warning("FormulaLibrary: cannot open file: " + path);
        return 0;
    }

    try
    {
        nlohmann::json j = nlohmann::json::parse(file);
        return loadFromJson(j);
    }
    catch (const std::exception& e)
    {
        Logger::warning("FormulaLibrary: JSON parse error in " + path + ": " + e.what());
        return 0;
    }
}

nlohmann::json FormulaLibrary::toJson() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [name, def] : m_formulas)
    {
        arr.push_back(def.toJson());
    }
    return arr;
}

bool FormulaLibrary::saveToFile(const std::string& path) const
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        Logger::warning("FormulaLibrary: cannot open file for writing: " + path);
        return false;
    }

    file << toJson().dump(4);
    return file.good();
}

// ---------------------------------------------------------------------------
// Built-in templates
// ---------------------------------------------------------------------------

void FormulaLibrary::registerBuiltinTemplates()
{
    auto templates = PhysicsTemplates::createAll();
    for (auto& tmpl : templates)
    {
        registerFormula(std::move(tmpl));
    }
}

} // namespace Vestige
