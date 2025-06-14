#include "MatterNode.h"
#include <esp_openthread.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

using namespace esp_matter::attribute;
using namespace chip::app::Clusters;

static const char *TAG = "MatterNode";
constexpr auto k_timeout_seconds = 300;

std::shared_ptr<MatterNode> MatterNode::s_instance = nullptr;

MatterNode::MatterNode(node_t* node)
    : m_node(node)
{
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;

    //Log the attribute update
    ESP_LOGI(TAG, "app_attribute_update_cb: type: %u, endpoint_id: %u, cluster_id: %lu, attribute_id: %lu",
             type, endpoint_id, cluster_id, attribute_id);

    // Access the singleton MatterNode instance
    auto node = MatterNode::GetInstance();
    if (!node) {
        ESP_LOGE(TAG, "MatterNode instance not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Look up the endpoint in the node's map
    auto endpoint = node->GetEndpoint(endpoint_id);
    if (!endpoint) {
        ESP_LOGW(TAG, "app_attribute_update_cb: Endpoint ID %u not found in node", endpoint_id);
        return ESP_OK; // Return OK for unhandled endpoints, as per callback contract
    }

    if (type == PRE_UPDATE) {
        err = endpoint->HandleAttributePreUpdate(cluster_id, attribute_id, val, priv_data);
    }

    return err;
}

static uint8_t GetMatterBootReason() {
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason) {
        case ESP_RST_POWERON:
            return 0x00; // PowerOnReset
        case ESP_RST_SW:
            return 0x03; // SoftwareReset
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return 0x04; // WatchdogReset
        case ESP_RST_BROWNOUT:
            return 0x02; // BrownoutReset
        case ESP_RST_PANIC:
            return 0x05; // CrashReset
        case ESP_RST_EXT:
        default:
            return 0xFF; // Unknown (Matter allows vendor-specific values >= 0x80)
    }
}

void AddSoftwareDiagnosticsCluster(node_t* node)
{
    endpoint_t* root_endpoint = endpoint::get(node, 0);

    cluster::software_diagnostics::config_t config;
    uint32_t features = cluster::software_diagnostics::feature::watermarks::get_id();
    cluster_t* cluster = cluster::software_diagnostics::create(root_endpoint, &config, CLUSTER_FLAG_SERVER, features);

    esp_matter::cluster::software_diagnostics::attribute::create_current_heap_free(cluster, 0);
    esp_matter::cluster::software_diagnostics::attribute::create_current_heap_used(cluster, 0);
    esp_matter::cluster::software_diagnostics::attribute::create_current_heap_high_watermark(cluster, 0);
    esp_matter::cluster::software_diagnostics::attribute::create_thread_metrics(cluster, nullptr, 0, 0);
}

void UpdateAttributeValueUInt16(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, uint16_t value)
{
    esp_matter_attr_val_t val = esp_matter_uint16(value);
    attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

void AddThreadNetworkDiagnosticsCluster(node_t* node)
{
    endpoint_t* root_endpoint = endpoint::get(node, 0);

    cluster::thread_network_diagnostics::config_t thread_network_config;
    cluster_t* cluster = cluster::thread_network_diagnostics::create(root_endpoint, &thread_network_config, CLUSTER_FLAG_SERVER);

    // ErrorCounts (ERRCNT)
    cluster::thread_network_diagnostics::feature::error_counts::add(cluster);

    // PacketCounts (PKTCNT)
    cluster::thread_network_diagnostics::feature::packets_counts::add(cluster);

    // MLECounts (MLECNT)
    cluster::thread_network_diagnostics::feature::mle_counts::add(cluster);
    esp_matter::cluster::thread_network_diagnostics::attribute::create_chlid_role_count(cluster, 0);
    esp_matter::cluster::thread_network_diagnostics::attribute::create_leader_role_count(cluster, 0);
    esp_matter::cluster::thread_network_diagnostics::attribute::create_router_role_count(cluster, 0);

    // MACCounts (MACCNT)
    cluster::thread_network_diagnostics::feature::mac_counts::add(cluster);
    esp_matter::cluster::thread_network_diagnostics::attribute::create_rx_total_count(cluster, 0);
    esp_matter::cluster::thread_network_diagnostics::attribute::create_tx_total_count(cluster, 0);

    // Create the RLOC16 attribute
    esp_matter::cluster::thread_network_diagnostics::attribute::create_rloc16(cluster, 0);
}

void AddWiFiNetworkDiagnosticsCluster(node_t* node)
{
    endpoint_t* root_endpoint = endpoint::get(node, 0);

    cluster::wifi_network_diagnostics::config_t cluster_config;
    cluster_t* cluster = cluster::wifi_network_diagnostics::create(root_endpoint, &cluster_config, CLUSTER_FLAG_SERVER);

    // ErrorCounts (ERRCNT)
    cluster::wifi_network_diagnostics::feature::error_counts::add(cluster);

    // PacketCounts (PKTCNT)
    cluster::wifi_network_diagnostics::feature::packet_counts::add(cluster);
}

void ConfigureGeneralDiagnosticsCluster(node_t* node)
{
    esp_matter::endpoint_t *rootEndpoint = esp_matter::endpoint::get(node, 0);
    cluster_t *cluster = cluster::get(rootEndpoint, GeneralDiagnostics::Id);

    uint8_t bootReason = GetMatterBootReason();
    cluster::general_diagnostics::attribute::create_boot_reason(cluster, bootReason);
}

std::shared_ptr<MatterNode> MatterNode::Create()
{
    // Check if instance already exists
    if (s_instance) {
        ESP_LOGW(TAG, "MatterNode instance already exists");
        return s_instance;
    }

    node::config_t node_config;
    node_t* node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE("MatterNode", "Failed to create Matter node");
        return nullptr;
    }

    // Create singleton instance
    s_instance = std::shared_ptr<MatterNode>(new MatterNode(node));
    if (!s_instance) {
        ESP_LOGE(TAG, "Failed to allocate MatterNode");
        return nullptr;
    }

    ESP_LOGI(TAG, "MatterNode created");

    ConfigureGeneralDiagnosticsCluster(s_instance->GetNode());
    AddSoftwareDiagnosticsCluster(s_instance->GetNode());

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    AddThreadNetworkDiagnosticsCluster(s_instance->GetNode());
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
    AddWiFiNetworkDiagnosticsCluster(s_instance->GetNode());
#endif

    return s_instance;
}

std::shared_ptr<MatterNode> MatterNode::GetInstance() {
    return s_instance;
}

void MatterNode::AddEndpoint(std::shared_ptr<MatterEndpoint> endpoint) {
    if (endpoint) {
        uint16_t id = endpoint->GetId(); // Use GetId() from MatterEndpoint
        m_endpoints[id] = std::move(endpoint); // Store shared_ptr in map
    }
}

std::shared_ptr<MatterEndpoint> MatterNode::GetEndpoint(uint16_t id) const
{
    auto it = m_endpoints.find(id);
    if (it == m_endpoints.end()) {
        return nullptr; // Return nullptr if endpoint not found
    }

    return it->second;
}

node_t* MatterNode::GetNode() const
{
    return m_node;
}

void MatterNode::Initialize()
{
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    //UpdateRloc16();
#endif
  
    // Call Initialize on all endpoints
    for (const auto& pair : m_endpoints) {
        auto endpoint = pair.second;
        if (endpoint) {
            ESP_LOGI(TAG, "Initializing endpoint with ID %u", endpoint->GetId());
            esp_err_t init_err = endpoint->Initialize();
            if (init_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initialize endpoint with ID %u, error: %s", endpoint->GetId(), esp_err_to_name(init_err));
            }
        }
    }
}

esp_err_t MatterNode::StartMatter()
{
    esp_err_t err = esp_matter::start(matter_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter, error: %s", esp_err_to_name(err));
        return err; // Return error if starting Matter fails
    }

    ESP_LOGI(TAG, "Matter started successfully");

    Initialize();

    return ESP_OK; // Return success if Matter started and all endpoints initialized successfully
}

void MatterNode::UpdateRloc16()
{
    // Get the RLOC16 value
    uint16_t rloc16 = otThreadGetRloc16(esp_openthread_get_instance());

    ESP_LOGI(TAG, "RLOC16: 0x%04X", rloc16);

    UpdateAttributeValueUInt16(
        0, // Endpoint ID 0 for the root endpoint
        ThreadNetworkDiagnostics::Id,
        ThreadNetworkDiagnostics::Attributes::Rloc16::Id,
        rloc16);
}

void MatterNode::matter_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        {
            ESP_LOGI(TAG, "Fabric removed successfully");
            if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
            {
                chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
                constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
                if (!commissionMgr.IsCommissioningWindowOpen())
                {
                    /* After removing last fabric, this example does not remove the Wi-Fi credentials
                     * and still has IP connectivity so, only advertising on DNS-SD.
                     */
                    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
                    if (err != CHIP_NO_ERROR)
                    {
                        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                    }
                }
            }
        break;
        }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}
