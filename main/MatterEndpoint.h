#pragma once

#include <esp_matter.h>
#include <memory>

using namespace esp_matter;

class MatterEndpoint
{
    public:

    uint16_t GetId();

    endpoint_t* GetEndpoint() const;

    // Initialize wll be called from MatterNode::StartMatter()
    // to allow the endpoint to perform any necessary setup after Matter
    // have started.
    virtual esp_err_t Initialize();
    
    virtual esp_err_t HandleAttributePreUpdate(
        uint32_t cluster_id,
        uint32_t attribute_id,
        esp_matter_attr_val_t* val,
        void *priv_data);    

    protected:

        MatterEndpoint(endpoint_t* endpoint);

        MatterEndpoint() = delete; // Prevent default constructor

        bool GetAttributeValueBool(uint32_t cluster_id, uint32_t attribute_id) const;

        uint8_t GetAttributeValueUInt8(uint32_t cluster_id, uint32_t attribute_id) const;

        uint16_t GetAttributeValueUInt16(uint32_t cluster_id, uint32_t attribute_id) const;

        void UpdateAttributeValueBool(uint32_t cluster_id, uint32_t attribute_id, bool value);

        void UpdateAttributeValueUInt8(uint32_t cluster_id, uint32_t attribute_id, uint8_t value);

        // Updates an int16 attribute for the given cluster and attribute IDs
        void UpdateAttributeValueInt16(uint32_t cluster_id, uint32_t attribute_id, int16_t value);

        // Updates a float attribute for the given cluster and attribute IDs
        void UpdateAttributeValueFloat(uint32_t cluster_id, uint32_t attribute_id, float value);

        endpoint_t* m_endpoint;

};