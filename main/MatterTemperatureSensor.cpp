#include "MatterTemperatureSensor.h"

#include <esp_err.h>
#include <esp_log.h>
#include <common_macros.h>
#include <cmath>

using namespace esp_matter::attribute;
using namespace chip::app::Clusters;

static const char *TAG = "MatterTemperatureSensor";

MatterTemperatureSensor::MatterTemperatureSensor(node_t* node, std::shared_ptr<TemperatureSensor> temperatureSensor)
        : MatterSensorBase(node, "MatterTemperatureSensor"), m_temperatureSensor(temperatureSensor)
{
}

std::shared_ptr<MatterTemperatureSensor> MatterTemperatureSensor::CreateEndpoint(
    std::shared_ptr<MatterNode> matterNode,
    std::shared_ptr<TemperatureSensor> temperatureSensor)
{
    // Create Temperature Endpoint
    esp_matter::endpoint::temperature_sensor::config_t temperature_config;
    endpoint_t* endpoint = esp_matter::endpoint::temperature_sensor::create(matterNode->GetNode(), &temperature_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create temperature sensor endpoint"));
    
    auto matterTemperatureSensor = std::shared_ptr<MatterTemperatureSensor>(new MatterTemperatureSensor(endpoint, temperatureSensor));
    matterNode->AddEndpoint(matterTemperatureSensor);

    return matterTemperatureSensor;   
}

void MatterTemperatureSensor::UpdateMeasurements()
{
    auto temperature = m_temperatureSensor->MeasureTemperature();
    // Check if the measurement is valid
    if (!temperature.has_value())
    {
        ESP_LOGE(TAG, "MeasureTemperature: Failed to read temperature");
        return;
    }
    
    m_temperatureMeasurement = temperature.value();
    ESP_LOGI(TAG, "MeasureTemperature: %f", m_temperatureMeasurement.value());

    // Need to use ScheduleLambda to execute the updates to the clusters on the Matter thread for thread safety
    ScheduleAttributeUpdate(&UpdateAttributes, this);
}

void MatterTemperatureSensor::UpdateAttributes(MatterTemperatureSensor* matterTemperature)
{
    matterTemperature->UpdateTemperatureMeasurementAttributes(matterTemperature->m_temperatureMeasurement);  
}