#pragma once

#include "MatterEndpoint.h"
#include <esp_matter.h>
#include <esp_err.h>
#include <esp_log.h>
#include <common_macros.h>
#include <optional>

using namespace esp_matter;
using namespace esp_matter::endpoint;

class MatterSensorBase : public MatterEndpoint
{
public:
    // Constructor initializes common members
    MatterSensorBase(endpoint_t* endpoint, const char* tag)
        : MatterEndpoint(endpoint), m_tag(tag) {}

    virtual ~MatterSensorBase() = default;

    // Pure virtual function for updating measurements
    virtual void UpdateMeasurements() = 0;

protected:

    // Updates the RelativeHumidityMeasurement cluster's MeasuredValue attribute with a scaled value (0.01% units)
    void UpdateRelativeHumidityMeasurementAttributes(std::optional<float> relativeHumidity);

    // Updates the TemperatureMeasurement cluster's MeasuredValue attribute with a scaled value (0.01°C units)
    void UpdateTemperatureMeasurementAttributes(std::optional<float> temperature);

    // Template method to schedule attribute updates on the Matter thread
    template <typename T>
    void ScheduleAttributeUpdate(void (*updateFunc)(T*), T* instance) {
        chip::DeviceLayer::SystemLayer().ScheduleLambda([=]() {
            updateFunc(instance);
        });
    }

    const char* m_tag; // For logging with sensor-specific tag
};