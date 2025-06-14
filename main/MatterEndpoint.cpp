#include "MatterEndpoint.h"

using namespace esp_matter::attribute;

static const char *TAG = "MatterEndpoint";

MatterEndpoint::MatterEndpoint(endpoint_t* endpoint) : m_endpoint(endpoint)
{
}

endpoint_t* MatterEndpoint::GetEndpoint() const
{
    return m_endpoint;
}

uint16_t MatterEndpoint::GetId()
{
    return endpoint::get_id(m_endpoint);
}

esp_err_t MatterEndpoint::Initialize()
{
    // logging the initialization
    uint16_t endpoint_id = GetId();
    ESP_LOGI(TAG, "MatterEndpoint::Initialize: Entering endpoint_id=%d", endpoint_id);

    return ESP_OK; // Default implementation does nothing
}

esp_err_t MatterEndpoint::HandleAttributePreUpdate(
    uint32_t cluster_id,
    uint32_t attribute_id,
    esp_matter_attr_val_t* val,
    void *priv_data)
{
    ESP_LOGI(TAG, "Pre-update for cluster %lu, attribute %lu", cluster_id, attribute_id);
    return ESP_OK;
}

bool MatterEndpoint::GetAttributeValueBool(uint32_t cluster_id, uint32_t attribute_id) const
{
    uint16_t endpoint_id = esp_matter::endpoint::get_id(m_endpoint);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t* attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    attribute::get_val(attribute, &val);

    return val.val.b;
}

uint8_t MatterEndpoint::GetAttributeValueUInt8(uint32_t cluster_id, uint32_t attribute_id) const
{
    uint16_t endpoint_id = esp_matter::endpoint::get_id(m_endpoint);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t* attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    attribute::get_val(attribute, &val);

    return val.val.u8;
}

uint16_t MatterEndpoint::GetAttributeValueUInt16(uint32_t cluster_id, uint32_t attribute_id) const
{
    uint16_t endpoint_id = esp_matter::endpoint::get_id(m_endpoint);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t* attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    attribute::get_val(attribute, &val);

    return val.val.u16;
}

void MatterEndpoint::UpdateAttributeValueBool(uint32_t cluster_id, uint32_t attribute_id, bool value)
{
    uint16_t endpoint_id = esp_matter::endpoint::get_id(m_endpoint);

    esp_matter_attr_val_t val = esp_matter_bool(value);

    esp_matter::attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

void MatterEndpoint::UpdateAttributeValueUInt8(uint32_t cluster_id, uint32_t attribute_id, uint8_t value)
{
    uint16_t endpoint_id = esp_matter::endpoint::get_id(m_endpoint);

    esp_matter_attr_val_t val = esp_matter_uint8(value);

    esp_matter::attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

void MatterEndpoint::UpdateAttributeValueInt16(uint32_t cluster_id, uint32_t attribute_id, int16_t value)
{
    if (!m_endpoint) {
        ESP_LOGE(TAG, "Endpoint not initialized.");
        return;
    }
    uint16_t endpoint_id = endpoint::get_id(m_endpoint);
    esp_matter_attr_val_t val = esp_matter_int16(value);
    attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

void MatterEndpoint::UpdateAttributeValueFloat(uint32_t cluster_id, uint32_t attribute_id, float value)
{
    if (!m_endpoint) {
        ESP_LOGE(TAG, "Endpoint not initialized.");
        return;
    }
    uint16_t endpoint_id = endpoint::get_id(m_endpoint);
    esp_matter_attr_val_t val = esp_matter_float(value);
    attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}