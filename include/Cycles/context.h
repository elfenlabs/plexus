#pragma once
#include <string>
#include <cstdint>
#include <map>
#include <mutex>

namespace Cycles {
    using ResourceID = uint32_t;
    
    class Context {
    public:
        ResourceID register_resource(const std::string& name);
        std::string get_name(ResourceID id) const;
    private:
        std::map<std::string, ResourceID> m_name_to_id;
        std::map<ResourceID, std::string> m_id_to_name;
        mutable std::mutex m_mutex;
        ResourceID m_next_id = 0;
    };
}
