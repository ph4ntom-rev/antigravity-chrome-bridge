#pragma once
#include "cdp_client.h"
#include "ext_bridge.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ag {

using json = nlohmann::json;

struct ResourceDef {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
};

class ResourceRegistry {
public:
    ResourceRegistry(CDPClient& cdp, ExtensionBridge& ext);

    json list_resources() const;
    json read_resource(const std::string& uri);

private:
    CDPClient& cdp_;
    ExtensionBridge& ext_;
    std::vector<ResourceDef> resources_;

    void register_all();
};

} // namespace ag
