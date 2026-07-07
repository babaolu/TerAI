#pragma once
// include/providers/provider_factory.h

#include "providers/base_provider.h"
#include <memory>
#include <string>

namespace terai {

class ProviderFactory {
public:
    // Create provider from config block (must contain "name" field)
    static std::unique_ptr<BaseProvider> create(const json& cfg);

    static std::vector<std::string> available_providers();
};

} // namespace terai
