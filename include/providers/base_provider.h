#pragma once
// include/providers/base_provider.h

#include "core/types.h"
#include "third_party/json.hpp"
#include <string>
#include <memory>

namespace terai {

using json = nlohmann::json;

class BaseProvider {
public:
    virtual ~BaseProvider() = default;

    virtual LLMResponse complete(const std::vector<Message>& messages,
                                 const std::string& system    = "",
                                 int                max_tokens = 4096,
                                 double             temperature = 0.7) = 0;

    virtual LLMResponse stream(const std::vector<Message>& messages,
                               const std::string& system    = "",
                               int                max_tokens = 4096,
                               double             temperature = 0.7,
                               StreamCallback     cb         = nullptr) = 0;

    const std::string& name()  const { return _name; }
    const std::string& model() const { return _model; }
    void set_model(const std::string& m) { _model = m; }

protected:
    std::string _name;
    std::string _model;
    std::string _api_key;
    std::string _base_url;

    static json messages_to_json(const std::vector<Message>& msgs) {
        json arr = json::array();
        for (auto& m : msgs)
            arr.push_back({{"role", m.role}, {"content", m.content}});
        return arr;
    }
};

} // namespace terai
