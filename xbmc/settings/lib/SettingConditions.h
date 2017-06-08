#pragma once
/*
 *      Copyright (C) 2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <map>
#include <set>
#include <string>
#include <utility>

#include "SettingDefinitions.h"
#include "utils/BooleanLogic.h"

class CSettingsManager;
class CSetting;

using SettingConditionCheck = bool (*)(const std::string &condition, const std::string &value, std::shared_ptr<const CSetting> setting, void *data);

class ISettingCondition
{
public:
  ISettingCondition(CSettingsManager *settingsManager)
    : m_settingsManager(settingsManager)
  { }
  virtual ~ISettingCondition() = default;

  virtual bool Check() const = 0;

protected:
  CSettingsManager *m_settingsManager;
};

class CSettingConditionItem : public CBooleanLogicValue, public ISettingCondition
{
public:
  CSettingConditionItem(CSettingsManager *settingsManager = nullptr)
    : ISettingCondition(settingsManager)
  { }
  virtual ~CSettingConditionItem() = default;
  
  virtual bool Deserialize(const TiXmlNode *node);
  virtual const char* GetTag() const { return SETTING_XML_ELM_CONDITION; }
  virtual bool Check() const;

protected:
  std::string m_name;
  std::string m_setting;
};

class CSettingConditionCombination : public CBooleanLogicOperation, public ISettingCondition
{
public:
  CSettingConditionCombination(CSettingsManager *settingsManager = nullptr)
    : ISettingCondition(settingsManager)
  { }
  virtual ~CSettingConditionCombination() = default;

  virtual bool Check() const;

private:
  virtual CBooleanLogicOperation* newOperation() { return new CSettingConditionCombination(m_settingsManager); }
  virtual CBooleanLogicValue* newValue() { return new CSettingConditionItem(m_settingsManager); }
};

class CSettingCondition : public CBooleanLogic, public ISettingCondition
{
public:
  CSettingCondition(CSettingsManager *settingsManager = nullptr);
  virtual ~CSettingCondition() = default;

  virtual bool Check() const;
};

class CSettingConditionsManager
{
public:
  CSettingConditionsManager() = default;
  CSettingConditionsManager(const CSettingConditionsManager&) = delete;
  CSettingConditionsManager const& operator=(CSettingConditionsManager const&) = delete;
  virtual ~CSettingConditionsManager() = default;

  void AddCondition(std::string condition);
  void AddCondition(std::string identifier, SettingConditionCheck condition, void *data = nullptr);

  bool Check(std::string condition, const std::string &value = "", std::shared_ptr<const CSetting> setting = std::shared_ptr<const CSetting>()) const;

private:
  using SettingConditionPair = std::pair<std::string, std::pair<SettingConditionCheck, void*>>;
  using SettingConditionMap = std::map<std::string, std::pair<SettingConditionCheck, void*>>;

  SettingConditionMap m_conditions;
  std::set<std::string> m_defines;
};
