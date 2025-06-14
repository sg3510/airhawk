#pragma once

#include <esp_matter.h>

#include "sensors/AirQualitySensor.h"
#include "MatterExtendedColorLight.h"
#include "Measurements.h"
#include "MatterSensorBase.h"

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters::AirQuality;

// Represents a Matter Air Quality Sensor device type, compliant with the Matter protocol.
// This class interfaces with a physical air quality sensor to read various environmental
// measurements (e.g., CO2, PM2.5, humidity, temperature) and updates the corresponding
// Matter clusters (e.g., Air Quality (0x002B), Temperature Measurement (0x0402),
// Relative Humidity Measurement (0x0405)) for a specified endpoint on a Matter node.
// It also controls a light endpoint to visualize air quality status.
class MatterAirQualitySensor : public MatterSensorBase
{
    public:

        static std::shared_ptr<MatterAirQualitySensor> CreateEndpoint(
            std::shared_ptr<MatterNode> matterNode,
            std::shared_ptr<AirQualitySensor> airQualitySensor,
            std::shared_ptr<MatterExtendedColorLight> lightEndpoint);

        void UpdateMeasurements() override;

    private:

        MatterAirQualitySensor(node_t* node, std::shared_ptr<AirQualitySensor> airQualitySensor, std::shared_ptr<MatterExtendedColorLight> lightEndpoint);

        // Map from MeasurementType to Matter cluster ID
        static const std::unordered_map<AirQualitySensor::MeasurementType, uint32_t> measurementTypeToClusterId;

        std::shared_ptr<AirQualitySensor> m_airQualitySensor;
        std::shared_ptr<MatterExtendedColorLight> m_lightEndpoint;
        Measurements m_measurements;

        void AddRelativeHumidityMeasurementCluster();

        void AddTemperatureMeasurementCluster();

        void AddCarbonDioxideConcentrationMeasurementCluster();

        void AddPm1ConcentrationMeasurementCluster();

        void AddPm25ConcentrationMeasurementCluster();

        void AddPm10ConcentrationMeasurementCluster();

        void AddNitrogenDioxideConcentrationMeasurementCluster();

        void AddTotalVolatileOrganicCompoundsConcentrationMeasurementCluster();

        void AddAirQualityClusterFeatures();

        void SetLightByAirQuality(AirQualityEnum airQuality);

        AirQualityEnum ClassifyAirQualityByCO2();

        AirQualityEnum ClassifyAirQualityByPM10();

        AirQualityEnum ClassifyAirQualityByPM25();

        static void UpdateAirQualityAttributes(MatterAirQualitySensor* airQuality);

};