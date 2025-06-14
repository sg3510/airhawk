#pragma once

#include <esp_matter.h>
#include "MatterEndpoint.h"
#include "MatterNode.h"
#include "MatterRGBLEDDriver.h"

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

class MatterExtendedColorLight : public MatterEndpoint
{

public:

    // Creates and configures a Matter endpoint for the extended color light.
    static std::shared_ptr<MatterExtendedColorLight>  CreateEndpoint(
        std::shared_ptr<MatterNode> matterNode);

    esp_err_t Initialize() override;

    bool GetOnOff() const;

    void SetLightOnOff(bool on);

    void SetLightLevelPercent(float levelPercent);

    void SetLightColorHSV(uint8_t hue, uint8_t saturation);

    esp_err_t HandleAttributePreUpdate(uint32_t cluster_id, uint32_t attribute_id,
                                    esp_matter_attr_val_t* val, void *priv_data) override;

private:

    MatterExtendedColorLight(endpoint_t* endpoint, std::unique_ptr<MatterRGBLEDDriver> matterRGBLEDDriver);

    std::unique_ptr<MatterRGBLEDDriver> m_matterRGBLEDDriver = nullptr;

    uint8_t GetBrightness() const;

    uint8_t GetHue() const;

    uint8_t GetSaturation() const;

    uint16_t GetTemperature() const;

    ColorControl::ColorMode GetColorMode() const;

    void SetColorMode(ColorControl::ColorMode colorMode);

};