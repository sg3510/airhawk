/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>
#include <esp_openthread.h>

#include <common_macros.h>
#include <app_priv.h>
#include <app_reset.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif
using namespace chip::DeviceLayer;
#endif

#include "MatterNode.h"
#include "MatterAirQualitySensor.h"
#include "MatterExtendedColorLight.h"
#include "MatterHumiditySensor.h"
#include "MatterTemperatureSensor.h"
#include "SensirionSEN66.h"

static constexpr uint32_t MEASUREMENT_SAMPLE_SECONDS = 60;

std::shared_ptr<MatterNode> matterNode;
std::shared_ptr<MatterAirQualitySensor> matterAirQualitySensor;
std::shared_ptr<MatterExtendedColorLight> matterExtendedColorLight;
std::shared_ptr<MatterTemperatureSensor> matterTemperatureSensor;
std::shared_ptr<MatterHumiditySensor> matterHumiditySensor;
esp_timer_handle_t sensor_timer_handle;

static const char *TAG = "app_main";
uint16_t light_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

// Timer callback to measure air quality
void UpdateSensorsTimerCallback(void *arg)
{
    matterAirQualitySensor->UpdateMeasurements();
    matterTemperatureSensor->UpdateMeasurements();
    matterHumiditySensor->UpdateMeasurements();
}

void StartUpdateSensorsTimer()
{
    // Setup periodic timer to update sensor measurements

    esp_timer_create_args_t timer_args = {
        .callback = &UpdateSensorsTimerCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK, // Run callback in a task (safer for I2C)
        .name = "update_sensors_timer",
        .skip_unhandled_events = true, // Skip if previous callback is still running
    };
    
    esp_err_t err = esp_timer_create(&timer_args, &sensor_timer_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
        return;
    }
    
    // Start the timer to trigger every 60 seconds (1 minute)
    err = esp_timer_start_periodic(sensor_timer_handle, MEASUREMENT_SAMPLE_SECONDS * 1000000ULL); // In microseconds
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Update sensors timer started.");
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize driver */
    app_driver_handle_t button_handle = app_driver_button_init();
    app_reset_button_register(button_handle);

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    matterNode = MatterNode::Create();
    ABORT_APP_ON_FAILURE(matterNode != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    matterExtendedColorLight = matterExtendedColorLight->CreateEndpoint(matterNode);
    ABORT_APP_ON_FAILURE(matterExtendedColorLight != nullptr, ESP_LOGE(TAG, "Failed to create extended color light endpoint"));

    // Note this is a global variable used in other source files.
    light_endpoint_id = matterExtendedColorLight->GetId();

    ESP_LOGI(TAG, "Light created with endpoint_id %d", light_endpoint_id);

    std::shared_ptr<AirQualitySensor> airQualitySensor = std::make_shared<SensirionSEN66>(25.0f);
    airQualitySensor->Init();

    // Create Matter Air Quality Sensor Endpoint
    matterAirQualitySensor = MatterAirQualitySensor::CreateEndpoint(matterNode, airQualitySensor, matterExtendedColorLight);

    // Create Matter Temperature Sensor Endpoint
    matterTemperatureSensor = MatterTemperatureSensor::CreateEndpoint(matterNode, airQualitySensor);

    // Create Humidity Sensor Endpoint
    matterHumiditySensor = MatterHumiditySensor::CreateEndpoint(matterNode, airQualitySensor);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD && CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
    // Enable secondary network interface
    secondary_network_interface::config_t secondary_network_interface_config;
    endpoint = endpoint::secondary_network_interface::create(node, &secondary_network_interface_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create secondary network interface endpoint"));
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    auto * dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
    static_cast<ESP32SecureCertDACProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
    static_cast<ESP32FactoryDataProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#endif
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

    /* Matter start */
    err = matterNode->StartMatter();
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    StartUpdateSensorsTimer();

#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif
}

