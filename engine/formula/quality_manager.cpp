/// @file quality_manager.cpp
/// @brief Formula quality tier manager implementation.
#include "formula/quality_manager.h"

namespace Vestige
{

void FormulaQualityManager::setGlobalTier(QualityTier tier)
{
    m_globalTier = tier;
}

void FormulaQualityManager::setCategoryTier(const std::string& category,
                                             QualityTier tier)
{
    m_categoryOverrides[category] = tier;
}

QualityTier FormulaQualityManager::getCategoryTier(const std::string& category) const
{
    auto it = m_categoryOverrides.find(category);
    if (it != m_categoryOverrides.end())
    {
        return it->second;
    }
    return m_globalTier;
}

bool FormulaQualityManager::hasCategoryOverride(const std::string& category) const
{
    return m_categoryOverrides.find(category) != m_categoryOverrides.end();
}

void FormulaQualityManager::clearCategoryOverride(const std::string& category)
{
    m_categoryOverrides.erase(category);
}

void FormulaQualityManager::clearAllOverrides()
{
    m_categoryOverrides.clear();
}

QualityTier FormulaQualityManager::getEffectiveTier(const std::string& category) const
{
    return getCategoryTier(category);
}

nlohmann::json FormulaQualityManager::toJson() const
{
    nlohmann::json j;
    j["globalTier"] = qualityTierToString(m_globalTier);

    if (!m_categoryOverrides.empty())
    {
        nlohmann::json overrides = nlohmann::json::object();
        for (const auto& [category, tier] : m_categoryOverrides)
        {
            overrides[category] = qualityTierToString(tier);
        }
        j["categoryOverrides"] = overrides;
    }

    return j;
}

void FormulaQualityManager::fromJson(const nlohmann::json& j)
{
    if (j.contains("globalTier") && j["globalTier"].is_string())
    {
        m_globalTier = qualityTierFromString(j["globalTier"].get<std::string>());
    }

    m_categoryOverrides.clear();
    if (j.contains("categoryOverrides") && j["categoryOverrides"].is_object())
    {
        for (const auto& [key, val] : j["categoryOverrides"].items())
        {
            if (val.is_string())
            {
                m_categoryOverrides[key] = qualityTierFromString(val.get<std::string>());
            }
        }
    }
}

} // namespace Vestige
