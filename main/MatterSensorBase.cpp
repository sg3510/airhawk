#include <cmath>
#include "MatterSensorBase.h"

void MatterSensorBase::UpdateRelativeHumidityMeasurementAttributes(std::optional<float> relativeHumidity)
{
    if (!relativeHumidity.has_value()) {
        ESP_LOGE(m_tag, "Relative humidity measurement invalid.");
        return;
    }
    ESP_LOGI(m_tag, "Relative Humidity: %f", relativeHumidity.value());
    uint16_t reportedHumidity = static_cast<uint16_t>(std::round(relativeHumidity.value() * 100));
    UpdateAttributeValueInt16(
        chip::app::Clusters::RelativeHumidityMeasurement::Id,
        chip::app::Clusters::RelativeHumidityMeasurement::Attributes::MeasuredValue::Id,
        reportedHumidity);
}

void MatterSensorBase::UpdateTemperatureMeasurementAttributes(std::optional<float> temperature)
{
    if (!temperature.has_value()) {
        ESP_LOGE(m_tag, "Temperature measurement invalid.");
        return;
    }
    ESP_LOGI(m_tag, "Temperature: %f", temperature.value());
    int16_t reportedTemperature = static_cast<int16_t>(std::round(temperature.value() * 100));
    UpdateAttributeValueInt16(
        chip::app::Clusters::TemperatureMeasurement::Id,
        chip::app::Clusters::TemperatureMeasurement::Attributes::MeasuredValue::Id,
        reportedTemperature);
}