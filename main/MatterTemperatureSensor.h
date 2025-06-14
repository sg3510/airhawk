#pragma once

#include <esp_matter.h>

#include "sensors/TemperatureSensor.h"
#include "MatterNode.h"
#include "MatterSensorBase.h"

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters::AirQuality;

// Represents a Matter Temperature Sensor device type, compliant with the Matter protocol.
// This class interfaces with a physical temperature sensor to read temperature measurements
// and updates the corresponding Matter Temperature Measurement cluster (0x0402) attributes
// for a specified endpoint on a Matter node.
class MatterTemperatureSensor : public MatterSensorBase
{
    public:

        static std::shared_ptr<MatterTemperatureSensor> CreateEndpoint(
            std::shared_ptr<MatterNode> matterNode,
            std::shared_ptr<TemperatureSensor> temperatureSensor);

        // Reads the latest temperature measurement from the physical sensor and updates
        // the Matter Temperature Measurement cluster's MeasuredValue attribute.
        void UpdateMeasurements() override;

    private:

        MatterTemperatureSensor(endpoint_t* endpoint, std::shared_ptr<TemperatureSensor> temperatureSensor);

        std::shared_ptr<TemperatureSensor> m_temperatureSensor;
        std::optional<float> m_temperatureMeasurement;

        // Updates the Temperature Measurement cluster's attributes (e.g., MeasuredValue)
        // with the latest temperature reading for the specified MatterTemperatureSensor instance.
        // @param matterTemperature Pointer to the MatterTemperatureSensor instance to update.
        static void UpdateAttributes(MatterTemperatureSensor* matterTemperature);

};
