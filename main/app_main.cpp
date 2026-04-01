#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <bsp/esp-bsp.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <nvs_flash.h>

#include <app_openthread_config.h>
#include <app_reset.h>
#include <common_macros.h>

#include <drivers/sen66_driver.h>
#include <app/clusters/air-quality-server/air-quality-server.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// --- Attribute update helpers ---
// Each callback is called from the SEN66 polling timer. We schedule the Matter
// attribute update onto the Matter thread via ScheduleLambda.

static void on_temperature(uint16_t endpoint_id, float value, void *user_data)
{
    // Matter temperature: (°C) × 100, as int16
    int16_t val_scaled = static_cast<int16_t>(value * 100);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, val_scaled]() {
        esp_matter_attr_val_t val = esp_matter_nullable_int16(val_scaled);
        attribute::update(endpoint_id, TemperatureMeasurement::Id,
                          TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void on_humidity(uint16_t endpoint_id, float value, void *user_data)
{
    // Matter humidity: (%) × 100, as uint16
    uint16_t val_scaled = static_cast<uint16_t>(value * 100);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, val_scaled]() {
        esp_matter_attr_val_t val = esp_matter_nullable_uint16(val_scaled);
        attribute::update(endpoint_id, RelativeHumidityMeasurement::Id,
                          RelativeHumidityMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void on_co2(uint16_t endpoint_id, float value, void *user_data)
{
    // Concentration measurement: float in ppm
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, value]() {
        esp_matter_attr_val_t val = esp_matter_nullable_float(value);
        attribute::update(endpoint_id, CarbonDioxideConcentrationMeasurement::Id,
                          CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void on_pm1(uint16_t endpoint_id, float value, void *user_data)
{
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, value]() {
        esp_matter_attr_val_t val = esp_matter_nullable_float(value);
        attribute::update(endpoint_id, Pm1ConcentrationMeasurement::Id,
                          Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void on_pm25(uint16_t endpoint_id, float value, void *user_data)
{
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, value]() {
        esp_matter_attr_val_t val = esp_matter_nullable_float(value);
        attribute::update(endpoint_id, Pm25ConcentrationMeasurement::Id,
                          Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void on_pm10(uint16_t endpoint_id, float value, void *user_data)
{
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, value]() {
        esp_matter_attr_val_t val = esp_matter_nullable_float(value);
        attribute::update(endpoint_id, Pm10ConcentrationMeasurement::Id,
                          Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void on_voc(uint16_t endpoint_id, float value, void *user_data)
{
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, value]() {
        esp_matter_attr_val_t val = esp_matter_nullable_float(value);
        attribute::update(endpoint_id, TotalVolatileOrganicCompoundsConcentrationMeasurement::Id,
                          TotalVolatileOrganicCompoundsConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void on_nox(uint16_t endpoint_id, float value, void *user_data)
{
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, value]() {
        esp_matter_attr_val_t val = esp_matter_nullable_float(value);
        attribute::update(endpoint_id, NitrogenDioxideConcentrationMeasurement::Id,
                          NitrogenDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static chip::app::Clusters::AirQuality::Instance *s_air_quality_instance = nullptr;

static void on_air_quality(uint16_t endpoint_id, uint8_t air_quality, void *user_data)
{
    if (!s_air_quality_instance) return;
    auto aq_enum = static_cast<chip::app::Clusters::AirQuality::AirQualityEnum>(air_quality);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([aq_enum]() {
        s_air_quality_instance->UpdateAirQuality(aq_enum);
    });
}

// --- Matter event handling ---

static esp_err_t factory_reset_button_register()
{
    button_handle_t push_button;
    esp_err_t err = bsp_iot_button_create(&push_button, NULL, BSP_BUTTON_NUM);
    VerifyOrReturnError(err == ESP_OK, err);
    return app_reset_button_register(push_button);
}

static void open_commissioning_window_if_necessary()
{
    VerifyOrReturn(chip::Server::GetInstance().GetFabricTable().FabricCount() == 0);

    chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    VerifyOrReturn(!commissionMgr.IsCommissioningWindowOpen());

    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(300),
                                                                chip::CommissioningWindowAdvertisement::kDnssdOnly);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed successfully");
        open_commissioning_window_if_necessary();
        break;
    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    return ESP_OK;
}

// --- Helper to add a concentration measurement cluster to an endpoint ---
static esp_err_t add_concentration_cluster(endpoint_t *ep, uint32_t cluster_id)
{
    cluster::concentration_measurement::config_t config;
    config.measurement_medium = 0; // Air
    config.feature_flags = cluster::concentration_measurement::feature::numeric_measurement::get_id();
    cluster_t *cluster = cluster::concentration_measurement::create(ep, &config, CLUSTER_FLAG_SERVER, cluster_id);
    if (!cluster) {
        ESP_LOGE(TAG, "Failed to create concentration cluster 0x%lx", (unsigned long)cluster_id);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void app_main()
{
    nvs_flash_init();

    esp_err_t err = factory_reset_button_register();
    ABORT_APP_ON_FAILURE(ESP_OK == err, ESP_LOGE(TAG, "Failed to init reset button, err:%d", err));

    // Create Matter node
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    // --- Air Quality Sensor endpoint (with concentration clusters) ---
    air_quality_sensor::config_t aq_config;
    endpoint_t *aq_ep = air_quality_sensor::create(node, &aq_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(aq_ep != nullptr, ESP_LOGE(TAG, "Failed to create air_quality_sensor endpoint"));
    uint16_t aq_ep_id = endpoint::get_id(aq_ep);

    // Set up AirQuality server with all quality level features
    using AQFeature = chip::app::Clusters::AirQuality::Feature;
    uint32_t aq_features = static_cast<uint32_t>(AQFeature::kFair)
                         | static_cast<uint32_t>(AQFeature::kModerate)
                         | static_cast<uint32_t>(AQFeature::kVeryPoor)
                         | static_cast<uint32_t>(AQFeature::kExtremelyPoor);

    // Update the feature map attribute so controllers know what levels we support
    cluster_t *aq_cluster = cluster::get(aq_ep, AirQuality::Id);
    attribute_t *aq_feature_map = attribute::get(aq_cluster, chip::app::Clusters::Globals::Attributes::FeatureMap::Id);
    esp_matter_attr_val_t fm_val = esp_matter_bitmap32(aq_features);
    attribute::set_val(aq_feature_map, &fm_val);

    static chip::app::Clusters::AirQuality::Instance air_quality_instance(
        aq_ep_id, chip::BitMask<AQFeature>(aq_features));
    CHIP_ERROR chip_err = air_quality_instance.Init();
    if (chip_err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "AirQuality Instance Init failed");
    }
    s_air_quality_instance = &air_quality_instance;

    // Add concentration measurement clusters to the air quality endpoint
    add_concentration_cluster(aq_ep, CarbonDioxideConcentrationMeasurement::Id);
    add_concentration_cluster(aq_ep, Pm1ConcentrationMeasurement::Id);
    add_concentration_cluster(aq_ep, Pm25ConcentrationMeasurement::Id);
    add_concentration_cluster(aq_ep, Pm10ConcentrationMeasurement::Id);
    add_concentration_cluster(aq_ep, TotalVolatileOrganicCompoundsConcentrationMeasurement::Id);
    add_concentration_cluster(aq_ep, NitrogenDioxideConcentrationMeasurement::Id);

    // --- Temperature Sensor endpoint ---
    temperature_sensor::config_t temp_config;
    endpoint_t *temp_ep = temperature_sensor::create(node, &temp_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(temp_ep != nullptr, ESP_LOGE(TAG, "Failed to create temperature_sensor endpoint"));

    // --- Humidity Sensor endpoint ---
    humidity_sensor::config_t hum_config;
    endpoint_t *hum_ep = humidity_sensor::create(node, &hum_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(hum_ep != nullptr, ESP_LOGE(TAG, "Failed to create humidity_sensor endpoint"));

    // --- Initialize SEN66 driver with callbacks ---
    static sen66_driver_config_t sen66_config = {
        .temperature = { .cb = on_temperature, .endpoint_id = endpoint::get_id(temp_ep) },
        .humidity    = { .cb = on_humidity,    .endpoint_id = endpoint::get_id(hum_ep) },
        .co2         = { .cb = on_co2,         .endpoint_id = aq_ep_id },
        .pm1         = { .cb = on_pm1,         .endpoint_id = aq_ep_id },
        .pm25        = { .cb = on_pm25,        .endpoint_id = aq_ep_id },
        .pm10        = { .cb = on_pm10,        .endpoint_id = aq_ep_id },
        .voc         = { .cb = on_voc,         .endpoint_id = aq_ep_id },
        .nox         = { .cb = on_nox,         .endpoint_id = aq_ep_id },
        .air_quality = { .cb = on_air_quality,  .endpoint_id = aq_ep_id },
    };
    err = sen66_driver_init(&sen66_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SEN66 init failed (err:%d) - sensor may not be connected. Matter stack will still start.", err);
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    esp_openthread_platform_config_t ot_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
#endif

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
}
