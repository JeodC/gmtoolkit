
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Underanalyzer {

class GMEnumValue {
  public:
    std::string Name;
    int64_t Value;

    GMEnumValue(std::string name, int64_t value) : Name(std::move(name)), Value(value) {
    }
};

class GMEnum {
  public:
    std::string Name;

    GMEnum(std::string name, std::vector<std::shared_ptr<GMEnumValue>> values);
    GMEnum(const GMEnum& existing);

    const std::vector<std::shared_ptr<GMEnumValue>>& Values() const {
        return _values;
    }

    void AddNewValuesFrom(const GMEnum& other);
    bool ContainsValue(const std::string& name) const;
    bool TryGetValue(const std::string& name, int64_t& value) const;
    std::shared_ptr<GMEnumValue> FindValue(int64_t value) const;
    void AddValue(const std::string& name, int64_t value);

  private:
    std::vector<std::shared_ptr<GMEnumValue>> _values;
    std::unordered_map<int64_t, std::shared_ptr<GMEnumValue>> _valueLookupByValue;
    std::unordered_map<std::string, std::shared_ptr<GMEnumValue>> _valueLookupByName;
};

} // namespace Underanalyzer
