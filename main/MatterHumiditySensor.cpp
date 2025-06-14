#include "MatterHumiditySensor.h"

#include <esp_err.h>
#include <esp_log.h>
#include <common_macros.h>
#include <cmath>

using namespace esp_matter::attribute;
using namespace chip::app::Clusters;

static const char *TAG = "MatterHumiditySensor";

MatterHumiditySensor::MatterHumiditySensor(endpoint_t* endpoint, std::shared_ptr<RelativeHumiditySensor> humiditySensor)
        : MatterSensorBase(endpoint, "MatterHumiditySensor"), m_humiditySensor(humiditySensor)
{    
}

std::shared_ptr<MatterHumiditySensor> MatterHumiditySensor::CreateEndpoint(
    std::shared_ptr<MatterNode> matterNode,
    std::shared_ptr<RelativeHumiditySensor> humiditySensor)
{
    // Create Humidity Endpoint
    esp_matter::endpoint::humidity_sensor::config_t humidity_config;
    endpoint_t* endpoint = esp_matter::endpoint::humidity_sensor::create(matterNode->GetNode(), &humidity_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create humidity sensor endpoint."));
    
    auto matterHumiditySensor = std::shared_ptr<MatterHumiditySensor>(new MatterHumiditySensor(endpoint, humiditySensor));
    matterNode->AddEndpoint(matterHumiditySensor);

    return matterHumiditySensor; 
}

void MatterHumiditySensor::UpdateMeasurements()
{
    auto relativeHumidity = m_humiditySensor->MeasureRelativeHumidity();
    // Check if the measurement is valid
    if (!relativeHumidity.has_value())
    {
        ESP_LOGE(TAG, "MeasureRelativeHumidity: Failed to read humidity");
        return;
    }

    m_humidityMeasurement = relativeHumidity.value();
    ESP_LOGI(TAG, "MeasureRelativeHumidity: %f", m_humidityMeasurement.value());

    // Need to use ScheduleLambda to execute the updates to the clusters on the Matter thread for thread safety
    ScheduleAttributeUpdate(&UpdateAttributes, this);
}

void MatterHumiditySensor::UpdateAttributes(MatterHumiditySensor* matterHumidity)
{
    matterHumidity->UpdateRelativeHumidityMeasurementAttributes(matterHumidity->m_humidityMeasurement);
}