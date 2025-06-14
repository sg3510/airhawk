#include "MatterExtendedColorLight.h"

#include <esp_err.h>
#include <esp_log.h>
#include <device.h>
#include <led_driver.h>

using namespace chip::app::Clusters;

static const char *TAG = "MatterExtendedColorLight";

MatterExtendedColorLight::MatterExtendedColorLight(endpoint_t* endpoint, std::unique_ptr<MatterRGBLEDDriver> matterRGBLEDDriver)
    : MatterEndpoint(endpoint), m_matterRGBLEDDriver(std::move(matterRGBLEDDriver))
{
}

std::shared_ptr<MatterExtendedColorLight> MatterExtendedColorLight::CreateEndpoint(std::shared_ptr<MatterNode> matterNode)
{
    auto matterRGBLEDDriver = std::unique_ptr<MatterRGBLEDDriver>(MatterRGBLEDDriver::Create());
    if (matterRGBLEDDriver == nullptr) {
        ESP_LOGE(TAG, "Failed to create MatterRGBLEDDriver");
        return nullptr;
    }

    extended_color_light::config_t light_config;
    light_config.on_off.features.lighting.start_up_on_off = nullptr;
    light_config.level_control.features.lighting.start_up_current_level = nullptr;
    light_config.color_control.color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    light_config.color_control.enhanced_color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    

    // Set the feature flags for the On Off cluster
    light_config.on_off.feature_flags = cluster::on_off::feature::lighting::get_id();

    // Set the feature flags for the Level Control cluster
    light_config.level_control.feature_flags = cluster::level_control::feature::lighting::get_id();

    // Set the feature flags for the Color Control cluster
    light_config.color_control.feature_flags = cluster::color_control::feature::hue_saturation::get_id();

    // Create Extended Color Light Endpoint
    ESP_LOGI(TAG, "Creating extended color light endpoint");
    endpoint_t* endpoint = extended_color_light::create(matterNode->GetNode(), &light_config, ENDPOINT_FLAG_NONE, nullptr);
    if (endpoint == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create extended color light endpoint");
        return nullptr;
    }

    ESP_LOGI(TAG, "Created extended color light endpoint");

    uint16_t light_endpoint_id = endpoint::get_id(endpoint);

        /* Mark deferred persistence for some attributes that might be changed rapidly */
    attribute_t *current_level_attribute = attribute::get(light_endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::set_deferred_persistence(current_level_attribute);

    attribute_t *current_x_attribute = attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
    attribute::set_deferred_persistence(current_x_attribute);
    attribute_t *current_y_attribute = attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
    attribute::set_deferred_persistence(current_y_attribute);
    attribute_t *color_temp_attribute = attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
    attribute::set_deferred_persistence(color_temp_attribute);

    auto matterExtendedColorLight = std::shared_ptr<MatterExtendedColorLight>(
        new MatterExtendedColorLight(endpoint, std::move(matterRGBLEDDriver)));
    matterNode->AddEndpoint(matterExtendedColorLight);

    bool onOff = matterExtendedColorLight->GetOnOff();
    ESP_LOGI(TAG, "CreateEndpoint: LED power state is %s", onOff ? "ON" : "OFF");

    return matterExtendedColorLight;
}

esp_err_t MatterExtendedColorLight::Initialize()
{
    // logging the initialization
    uint16_t endpoint_id = GetId();
    ESP_LOGI(TAG, "Initialize: Entering endpoint_id=%d", endpoint_id);
    esp_err_t err = ESP_OK;

    bool onOff = GetOnOff();
    ESP_LOGI(TAG, "Initialize: LED power state is %s", onOff ? "ON" : "OFF");

    /* Starting driver with default values */

    /* Setting brightness */
    uint8_t brightness = GetBrightness();
    err |= m_matterRGBLEDDriver->SetBrightness(brightness);

    /* Setting color */
    ColorControl::ColorMode colorMode = GetColorMode();
    if (colorMode == ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
        /* Setting hue */
        uint8_t hue = GetHue();
        err |= m_matterRGBLEDDriver->SetHue(hue);
        /* Setting saturation */
        uint8_t saturation = GetSaturation();
        err |= m_matterRGBLEDDriver->SetSaturation(saturation);
    } else if (colorMode == ColorControl::ColorMode::kColorTemperature) {
        /* Setting temperature */
        uint16_t temperature = GetTemperature();
        err |= m_matterRGBLEDDriver->SetTemperature(temperature);
    } else {
        ESP_LOGE(TAG, "Color mode not supported");
    }

    /* Setting power */
    onOff = GetOnOff();
    ESP_LOGI(TAG, "LED power state is %s", onOff ? "ON" : "OFF");
    err |= m_matterRGBLEDDriver->SetPower(onOff);

    return err;
}

esp_err_t MatterExtendedColorLight::HandleAttributePreUpdate(
    uint32_t cluster_id,
    uint32_t attribute_id,
    esp_matter_attr_val_t* val,
    void *priv_data)
{
    uint16_t endpoint_id = GetId();
    ESP_LOGI(TAG, "HandleAttributePreUpdate: Entering endpoint_id=%d, cluster_id=%lu, attribute_id=%lu", endpoint_id, cluster_id, attribute_id);

    esp_err_t err = ESP_OK;

    if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            err = m_matterRGBLEDDriver->SetPower(val->val.b);
        }
    } else if (cluster_id == LevelControl::Id) {
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            err = m_matterRGBLEDDriver->SetBrightness(val->val.u8);
        }
    } else if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            err = m_matterRGBLEDDriver->SetHue(val->val.u8);
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            err = m_matterRGBLEDDriver->SetSaturation(val->val.u8);
        } else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            err = m_matterRGBLEDDriver->SetTemperature(val->val.u16);
        }
    }

    return err;
}

void MatterExtendedColorLight::SetLightOnOff(bool on)   
{
    UpdateAttributeValueBool(
        OnOff::Id,
        OnOff::Attributes::OnOff::Id,
        on);
}

void MatterExtendedColorLight::SetLightLevelPercent(float levelPercent)
{
    // For the CurrentLevel attribute:
    // A value of 0x00 SHALL NOT be used.
    // A value of 0x01 SHALL indicate the minimum level that can be attained on a device.
    // A value of 0xFE SHALL indicate the maximum level that can be attained on a device.
    // A value of null SHALL represent an undefined value.
    // All other values are application specific gradations from the minimum to the maximum level. 
    uint8_t level = static_cast<uint8_t>((levelPercent / 100.0f) * 0xFD) + 0x01;

    UpdateAttributeValueUInt8(
        LevelControl::Id,
        LevelControl::Attributes::CurrentLevel::Id,
        level);    
}

void MatterExtendedColorLight::SetLightColorHSV(uint8_t hue, uint8_t saturation)
{
    // Hue represents the color. It ranges from 0 to 360 degrees.
    // 0 degrees = Red
    // 120 degrees = Green
    // 240 degrees = Blue
    // In matter it's representet as a byte with the range 0 to 255.

    ESP_LOGI(TAG, "SetLightColorHSV: CurrentHue=%d CurrentSaturation=%d" , hue, saturation);

    // Update ColorMode to kCurrentHueAndCurrentSaturation
    //SetColorMode(ColorControl::ColorMode::kCurrentHueAndCurrentSaturation);

    // Update CurrentHue
    UpdateAttributeValueUInt8(
        ColorControl::Id,
        ColorControl::Attributes::CurrentHue::Id,
        hue);

    // Update CurrentSaturation
    UpdateAttributeValueUInt8(
        ColorControl::Id,
        ColorControl::Attributes::CurrentSaturation::Id,
        saturation);
}

bool MatterExtendedColorLight::GetOnOff() const
{
    bool onOff = GetAttributeValueBool(
        OnOff::Id,
        OnOff::Attributes::OnOff::Id);
    
    return onOff;
}

uint8_t MatterExtendedColorLight::GetBrightness() const
{
    uint8_t brightness = GetAttributeValueUInt8(
        LevelControl::Id,
        LevelControl::Attributes::CurrentLevel::Id);
    
    return brightness;
}

uint8_t MatterExtendedColorLight::GetHue() const
{
    uint8_t hue = GetAttributeValueUInt8(
        ColorControl::Id,
        ColorControl::Attributes::CurrentHue::Id);
    
    return hue;
}

uint8_t MatterExtendedColorLight::GetSaturation() const
{
    uint8_t saturation = GetAttributeValueUInt8(
        ColorControl::Id,
        ColorControl::Attributes::CurrentSaturation::Id);
    
    return saturation;
}

uint16_t MatterExtendedColorLight::GetTemperature() const
{
    uint16_t temperature = GetAttributeValueUInt16(
        ColorControl::Id,
        ColorControl::Attributes::ColorTemperatureMireds::Id);
    
    return temperature;
}

ColorControl::ColorMode MatterExtendedColorLight::GetColorMode() const
{
    uint8_t colorMode = GetAttributeValueUInt8(
        ColorControl::Id,
        ColorControl::Attributes::ColorMode::Id);
    
    return static_cast<ColorControl::ColorMode>(colorMode);
}

void MatterExtendedColorLight::SetColorMode(ColorControl::ColorMode colorMode)
{
    UpdateAttributeValueUInt8(
        ColorControl::Id,
        ColorControl::Attributes::ColorMode::Id,
        static_cast<uint8_t>(colorMode));
}