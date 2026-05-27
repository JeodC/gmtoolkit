
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/GMEnum.h"

namespace Underanalyzer {

GMEnum::GMEnum(std::string name, std::vector<std::shared_ptr<GMEnumValue>> values)
    : Name(std::move(name)), _values(std::move(values)) {
    _valueLookupByValue.reserve(_values.size());
    _valueLookupByName.reserve(_values.size());
    for (auto& value : _values) {
        _valueLookupByValue[value->Value] = value;
        _valueLookupByName[value->Name] = value;
    }
}

GMEnum::GMEnum(const GMEnum& existing)
    : Name(existing.Name), _values(existing._values), _valueLookupByValue(existing._valueLookupByValue),
      _valueLookupByName(existing._valueLookupByName) {
}

void GMEnum::AddNewValuesFrom(const GMEnum& other) {
    for (const auto& value : other._values) {
        if (_valueLookupByName.find(value->Name) == _valueLookupByName.end()) {
            _values.push_back(value);
            _valueLookupByValue[value->Value] = value;
            _valueLookupByName[value->Name] = value;
        }
    }
}

bool GMEnum::ContainsValue(const std::string& name) const {
    return _valueLookupByName.find(name) != _valueLookupByName.end();
}

bool GMEnum::TryGetValue(const std::string& name, int64_t& value) const {
    auto it = _valueLookupByName.find(name);
    if (it == _valueLookupByName.end())
        return false;
    value = it->second->Value;
    return true;
}

std::shared_ptr<GMEnumValue> GMEnum::FindValue(int64_t value) const {
    auto it = _valueLookupByValue.find(value);
    if (it == _valueLookupByValue.end())
        return nullptr;
    return it->second;
}

void GMEnum::AddValue(const std::string& name, int64_t value) {
    auto entry = std::make_shared<GMEnumValue>(name, value);
    _values.push_back(entry);
    _valueLookupByValue[value] = entry;
    _valueLookupByName[name] = entry;
}

} // namespace Underanalyzer
