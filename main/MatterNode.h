#pragma once

#include "MatterEndpoint.h"
#include <esp_matter.h>
#include <memory>
#include <map>

using namespace esp_matter;

class MatterNode
{

    public:

        static std::shared_ptr<MatterNode> Create();

        static std::shared_ptr<MatterNode> GetInstance();

        void AddEndpoint(std::shared_ptr<MatterEndpoint> endpoint);

        // Method to find an endpoint by ID
        std::shared_ptr<MatterEndpoint> GetEndpoint(uint16_t id) const;

        // Method to get the underlying Matter node
        node_t* GetNode() const;

        void Initialize();

        esp_err_t StartMatter();

    private:

        MatterNode(node_t* node);

        MatterNode() = delete; // Prevent default constructor

        void UpdateRloc16();

        static void matter_event_cb(const ChipDeviceEvent *event, intptr_t arg);

        node_t* m_node;
        static std::shared_ptr<MatterNode> s_instance;
        std::map<uint16_t, std::shared_ptr<MatterEndpoint>> m_endpoints;

};