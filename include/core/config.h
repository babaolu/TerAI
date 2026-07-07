#pragma once
// include/core/config.h

#include <string>
#include <vector>
#include "third_party/json.hpp"

namespace terai {

using json = nlohmann::json;

class Config {
public:
    Config();

    // Get value by dot-path: get("providers","anthropic","api_key")
    template<typename T = std::string>
    T get(const std::string& k1,
          const std::string& k2 = "",
          const std::string& k3 = "",
          T                  def = T{}) const;

    void set(const std::string& k1, const json& value);
    void set(const std::string& k1, const std::string& k2, const json& value);
    void set(const std::string& k1, const std::string& k2,
             const std::string& k3, const json& value);

    // Returns merged provider config for the active provider
    json active_provider_config() const;

    void display() const;

    const json& data() const { return _data; }

    static std::string config_dir();
    static std::string config_file();

private:
    json _data;
    void load();
    void save() const;
    static json defaults();
    static json deep_merge(const json& base, const json& override);
};

} // namespace terai
