/*
 * ESP32-S3 Modbus RTU Slave Example with MAX485
 * 
 * This example implements a Modbus RTU slave with two holding registers:
 * - Register 0: Sequential counter (increments on each access)
 * - Register 1: Random number (updated periodically)
 * - Register 2: Second counter (auto-reset)
 * - Registers 3-9: General purpose holding registers
 * 
 * Features:
 * - WiFi AP active for 2 minutes after boot for configuration
 * - Web interface to configure slave ID and view statistics
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "mbcontroller.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#define MB_PORT_NUM     (1)         // UART port number for Modbus
#define MB_SLAVE_ADDR   (1)         // Modbus slave address
#define MB_DEV_SPEED    (9600)      // Modbus communication speed (9600 for RS485)
#define MB_UART_TXD     (18)        // TX pin for HW-519 TXD
#define MB_UART_RXD     (16)        // RX pin for HW-519 RXD

// WiFi AP Configuration
#define WIFI_AP_SSID        "ESP32-Modbus-Config"
#define WIFI_AP_PASSWORD    "modbus123"
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4
#define AP_TIMEOUT_MINUTES  20
#define AP_TIMEOUT_MS       (AP_TIMEOUT_MINUTES * 60 * 1000)

// Modbus register definitions
#define MB_REG_HOLDING_START    (0)
#define MB_REG_HOLDING_SIZE     (10)  // 10 holding registers

#define MB_PAR_INFO_GET_TOUT    (10) // Timeout for get parameter info

#define MB_READ_MASK            (MB_EVENT_HOLDING_REG_RD)
#define MB_WRITE_MASK           (MB_EVENT_HOLDING_REG_WR)
#define MB_READ_WRITE_MASK      (MB_READ_MASK | MB_WRITE_MASK)

static const char *TAG = "MB_SLAVE";

// Statistics
static struct {
    uint32_t total_requests;
    uint32_t read_requests;
    uint32_t write_requests;
    uint32_t errors;
    uint32_t uptime_seconds;
} modbus_stats = {0};

// Configuration stored in NVS
static uint8_t configured_slave_addr = MB_SLAVE_ADDR;

// WiFi state
static httpd_handle_t server = NULL;
static bool ap_active = false;
static TimerHandle_t ap_timer = NULL;

// Holding registers storage
#pragma pack(push, 1)
typedef struct {
    uint16_t sequential_counter;  // Register 0: Sequential counter
    uint16_t random_number;       // Register 1: Random number
    uint16_t holding_reg[8];      // Registers 2-9: General purpose holding registers
} holding_reg_params_t;
#pragma pack(pop)

holding_reg_params_t holding_reg_params = { 0 };

// Initialize register data
static void setup_reg_data(void)
{
    holding_reg_params.sequential_counter = 0;
    holding_reg_params.random_number = (uint16_t)(esp_random() % 65536);
    
    // Initialize additional holding registers with test values
    for (int i = 0; i < 8; i++) {
        holding_reg_params.holding_reg[i] = 100 + i;
    }
    
    ESP_LOGI(TAG, "Holding registers initialized:");
    ESP_LOGI(TAG, "  Register 0 (Sequential Counter): %u", holding_reg_params.sequential_counter);
    ESP_LOGI(TAG, "  Register 1 (Random Number): %u", holding_reg_params.random_number);
    ESP_LOGI(TAG, "  Register 2 (Second Counter): %u", holding_reg_params.holding_reg[0]);
    ESP_LOGI(TAG, "  Registers 3-9: %u, %u, %u, %u, %u, %u, %u",
             holding_reg_params.holding_reg[1], holding_reg_params.holding_reg[2],
             holding_reg_params.holding_reg[3], holding_reg_params.holding_reg[4],
             holding_reg_params.holding_reg[5], holding_reg_params.holding_reg[6],
             holding_reg_params.holding_reg[7]);
}

// Load configuration from NVS
static void load_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        uint8_t addr = MB_SLAVE_ADDR;
        nvs_get_u8(nvs_handle, "slave_addr", &addr);
        configured_slave_addr = addr;
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Loaded slave address from NVS: %d", configured_slave_addr);
    } else {
        configured_slave_addr = MB_SLAVE_ADDR;
        ESP_LOGI(TAG, "Using default slave address: %d", configured_slave_addr);
    }
}

// Save configuration to NVS
static esp_err_t save_config(uint8_t slave_addr)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_u8(nvs_handle, "slave_addr", slave_addr);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            configured_slave_addr = slave_addr;
            ESP_LOGI(TAG, "Saved slave address to NVS: %d", slave_addr);
        }
    }
    nvs_close(nvs_handle);
    return err;
}

// HTTP handler for root page
static esp_err_t root_handler(httpd_req_t *req)
{
    // Send HTML in chunks to avoid stack overflow
    httpd_resp_set_type(req, "text/html");
    
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><head>");
    httpd_resp_sendstr_chunk(req, "<meta name='viewport' content='width=device-width,initial-scale=1'>");
    httpd_resp_sendstr_chunk(req, "<title>Modbus Config</title><style>");
    httpd_resp_sendstr_chunk(req, "body{font-family:Arial;margin:20px;background:#f0f0f0}");
    httpd_resp_sendstr_chunk(req, ".container{max-width:600px;margin:auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}");
    httpd_resp_sendstr_chunk(req, "h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}");
    httpd_resp_sendstr_chunk(req, ".stat{display:flex;justify-content:space-between;padding:10px;margin:5px 0;background:#f9f9f9;border-radius:4px}");
    httpd_resp_sendstr_chunk(req, ".label{font-weight:bold;color:#555}");
    httpd_resp_sendstr_chunk(req, ".value{color:#4CAF50;font-weight:bold}");
    httpd_resp_sendstr_chunk(req, "input[type=number]{width:100%;padding:8px;margin:8px 0;border:1px solid #ddd;border-radius:4px}");
    httpd_resp_sendstr_chunk(req, "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px}");
    httpd_resp_sendstr_chunk(req, "button:hover{background:#45a049}");
    httpd_resp_sendstr_chunk(req, ".info{background:#e7f3fe;border-left:4px solid #2196F3;padding:10px;margin:10px 0}");
    httpd_resp_sendstr_chunk(req, ".tabs{display:flex;border-bottom:2px solid #4CAF50;margin:20px 0}");
    httpd_resp_sendstr_chunk(req, ".tab{padding:10px 20px;cursor:pointer;background:#f0f0f0;border:none;margin-right:2px}");
    httpd_resp_sendstr_chunk(req, ".tab.active{background:#4CAF50;color:white}");
    httpd_resp_sendstr_chunk(req, ".tab-content{display:none}");
    httpd_resp_sendstr_chunk(req, ".tab-content.active{display:block}");
    httpd_resp_sendstr_chunk(req, ".reg-table{width:100%;border-collapse:collapse;margin:10px 0}");
    httpd_resp_sendstr_chunk(req, ".reg-table th,.reg-table td{padding:8px;border:1px solid #ddd;text-align:left}");
    httpd_resp_sendstr_chunk(req, ".reg-table th{background:#4CAF50;color:white}");
    httpd_resp_sendstr_chunk(req, "</style></head><body><div class='container'>");
    httpd_resp_sendstr_chunk(req, "<h1>ESP32 Modbus RTU Slave</h1>");
    httpd_resp_sendstr_chunk(req, "<div class='info'>WiFi AP will turn off in 2 minutes after boot</div>");
    httpd_resp_sendstr_chunk(req, "<div class='tabs'>");
    httpd_resp_sendstr_chunk(req, "<button class='tab active' onclick='showTab(0)'>Statistics</button>");
    httpd_resp_sendstr_chunk(req, "<button class='tab' onclick='showTab(1)'>Registers</button>");
    httpd_resp_sendstr_chunk(req, "<button class='tab' onclick='showTab(2)'>Configuration</button>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "<div class='tab-content active' id='tab0'>");
    httpd_resp_sendstr_chunk(req, "<h2>Statistics</h2>");
    httpd_resp_sendstr_chunk(req, "<div class='stat'><span class='label'>Total Requests:</span><span class='value' id='total'>-</span></div>");
    httpd_resp_sendstr_chunk(req, "<div class='stat'><span class='label'>Read Requests:</span><span class='value' id='reads'>-</span></div>");
    httpd_resp_sendstr_chunk(req, "<div class='stat'><span class='label'>Write Requests:</span><span class='value' id='writes'>-</span></div>");
    httpd_resp_sendstr_chunk(req, "<div class='stat'><span class='label'>Errors:</span><span class='value' id='errors'>-</span></div>");
    httpd_resp_sendstr_chunk(req, "<div class='stat'><span class='label'>Uptime:</span><span class='value' id='uptime'>-</span></div>");
    httpd_resp_sendstr_chunk(req, "<div class='stat'><span class='label'>Current Slave ID:</span><span class='value' id='current_id'>-</span></div>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "<div class='tab-content' id='tab1'>");
    httpd_resp_sendstr_chunk(req, "<h2>Holding Registers (0-9)</h2>");
    httpd_resp_sendstr_chunk(req, "<table class='reg-table'>");
    httpd_resp_sendstr_chunk(req, "<tr><th>Address</th><th>Value (Decimal)</th><th>Value (Hex)</th><th>Description</th></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>0</td><td id='reg0'>-</td><td id='reg0h'>-</td><td>Sequential Counter</td></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>1</td><td id='reg1'>-</td><td id='reg1h'>-</td><td>Random Number</td></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>2</td><td id='reg2'>-</td><td id='reg2h'>-</td><td>Second Counter (auto-reset)</td></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>3</td><td id='reg3'>-</td><td id='reg3h'>-</td><td>General Purpose</td></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>4</td><td id='reg4'>-</td><td id='reg4h'>-</td><td>General Purpose</td></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>5</td><td id='reg5'>-</td><td id='reg5h'>-</td><td>General Purpose</td></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>6</td><td id='reg6'>-</td><td id='reg6h'>-</td><td>General Purpose</td></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>7</td><td id='reg7'>-</td><td id='reg7h'>-</td><td>General Purpose</td></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>8</td><td id='reg8'>-</td><td id='reg8h'>-</td><td>General Purpose</td></tr>");
    httpd_resp_sendstr_chunk(req, "<tr><td>9</td><td id='reg9'>-</td><td id='reg9h'>-</td><td>General Purpose</td></tr>");
    httpd_resp_sendstr_chunk(req, "</table></div>");
    httpd_resp_sendstr_chunk(req, "<div class='tab-content' id='tab2'>");
    httpd_resp_sendstr_chunk(req, "<h2>Configuration</h2><form id='configForm'>");
    httpd_resp_sendstr_chunk(req, "<label>Modbus Slave ID (1-247):</label>");
    httpd_resp_sendstr_chunk(req, "<input type='number' id='slave_id' name='slave_id' min='1' max='247' required>");
    httpd_resp_sendstr_chunk(req, "<button type='submit'>Save & Apply</button></form></div>");
    httpd_resp_sendstr_chunk(req, "<script>");
    httpd_resp_sendstr_chunk(req, "function showTab(n){");
    httpd_resp_sendstr_chunk(req, "document.querySelectorAll('.tab').forEach((t,i)=>t.classList.toggle('active',i===n));");
    httpd_resp_sendstr_chunk(req, "document.querySelectorAll('.tab-content').forEach((t,i)=>t.classList.toggle('active',i===n));");
    httpd_resp_sendstr_chunk(req, "}");
    httpd_resp_sendstr_chunk(req, "function updateStats(){");
    httpd_resp_sendstr_chunk(req, "fetch('/api/stats').then(r=>r.json()).then(d=>{");
    httpd_resp_sendstr_chunk(req, "document.getElementById('total').textContent=d.total;");
    httpd_resp_sendstr_chunk(req, "document.getElementById('reads').textContent=d.reads;");
    httpd_resp_sendstr_chunk(req, "document.getElementById('writes').textContent=d.writes;");
    httpd_resp_sendstr_chunk(req, "document.getElementById('errors').textContent=d.errors;");
    httpd_resp_sendstr_chunk(req, "document.getElementById('uptime').textContent=d.uptime+'s';");
    httpd_resp_sendstr_chunk(req, "document.getElementById('current_id').textContent=d.slave_id;");
    httpd_resp_sendstr_chunk(req, "document.getElementById('slave_id').value=d.slave_id;");
    httpd_resp_sendstr_chunk(req, "});}");
    httpd_resp_sendstr_chunk(req, "function updateRegisters(){");
    httpd_resp_sendstr_chunk(req, "fetch('/api/registers').then(r=>r.json()).then(d=>{");
    httpd_resp_sendstr_chunk(req, "d.registers.forEach((v,i)=>{");
    httpd_resp_sendstr_chunk(req, "document.getElementById('reg'+i).textContent=v;");
    httpd_resp_sendstr_chunk(req, "document.getElementById('reg'+i+'h').textContent='0x'+v.toString(16).toUpperCase().padStart(4,'0');");
    httpd_resp_sendstr_chunk(req, "});});}");
    httpd_resp_sendstr_chunk(req, "updateStats();updateRegisters();");
    httpd_resp_sendstr_chunk(req, "setInterval(()=>{updateStats();updateRegisters();},2000);");
    httpd_resp_sendstr_chunk(req, "document.getElementById('configForm').addEventListener('submit',function(e){");
    httpd_resp_sendstr_chunk(req, "e.preventDefault();");
    httpd_resp_sendstr_chunk(req, "const id=document.getElementById('slave_id').value;");
    httpd_resp_sendstr_chunk(req, "fetch('/api/config?slave_id='+id,{method:'POST'})");
    httpd_resp_sendstr_chunk(req, ".then(r=>r.json())");
    httpd_resp_sendstr_chunk(req, ".then(d=>{alert(d.message);if(d.success)updateStats();});");;
    httpd_resp_sendstr_chunk(req, "});");
    httpd_resp_sendstr_chunk(req, "</script></div></body></html>");
    
    httpd_resp_sendstr_chunk(req, NULL);  // End chunked response
    return ESP_OK;
}

// HTTP handler for statistics API
static esp_err_t stats_handler(httpd_req_t *req)
{
    char json[256];
    snprintf(json, sizeof(json),
        "{\"total\":%lu,\"reads\":%lu,\"writes\":%lu,\"errors\":%lu,\"uptime\":%lu,\"slave_id\":%d}",
        modbus_stats.total_requests,
        modbus_stats.read_requests,
        modbus_stats.write_requests,
        modbus_stats.errors,
        modbus_stats.uptime_seconds,
        configured_slave_addr);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP handler for registers API
static esp_err_t registers_handler(httpd_req_t *req)
{
    char json[512];
    snprintf(json, sizeof(json),
        "{\"registers\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]}",
        holding_reg_params.sequential_counter,
        holding_reg_params.random_number,
        holding_reg_params.holding_reg[0],
        holding_reg_params.holding_reg[1],
        holding_reg_params.holding_reg[2],
        holding_reg_params.holding_reg[3],
        holding_reg_params.holding_reg[4],
        holding_reg_params.holding_reg[5],
        holding_reg_params.holding_reg[6],
        holding_reg_params.holding_reg[7]);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP handler for configuration API
static esp_err_t config_handler(httpd_req_t *req)
{
    char buf[100];
    int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));
    
    if (ret == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "slave_id", param, sizeof(param)) == ESP_OK) {
            int slave_id = atoi(param);
            if (slave_id >= 1 && slave_id <= 247) {
                esp_err_t err = save_config((uint8_t)slave_id);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Configuration saved. configured_slave_addr is now: %d", configured_slave_addr);
                    ESP_LOGI(TAG, "Requesting ESP32 restart to apply new slave address...");
                    
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Slave ID saved. ESP32 will restart in 2 seconds...\"}");
                    
                    // Restart after a delay to allow response to be sent
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_restart();
                    
                    return ESP_OK;
                } else {
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Failed to save configuration\"}");
                    return ESP_OK;
                }
            }
        }
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Invalid slave ID\"}");
    return ESP_OK;
}

// Start web server
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting web server on port: %d", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        
        httpd_uri_t stats_uri = {
            .uri = "/api/stats",
            .method = HTTP_GET,
            .handler = stats_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &stats_uri);
        
        httpd_uri_t registers_uri = {
            .uri = "/api/registers",
            .method = HTTP_GET,
            .handler = registers_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &registers_uri);
        
        httpd_uri_t config_uri = {
            .uri = "/api/config",
            .method = HTTP_POST,
            .handler = config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_uri);
        
        return server;
    }
    
    ESP_LOGE(TAG, "Error starting web server!");
    return NULL;
}

// Stop web server
static void stop_webserver(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// Timer callback to stop AP
static void ap_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "AP timeout reached - shutting down WiFi AP");
    
    if (server) {
        stop_webserver(server);
        server = NULL;
    }
    
    esp_wifi_stop();
    esp_wifi_deinit();
    ap_active = false;
    
    ESP_LOGI(TAG, "WiFi AP stopped - device now running in Modbus-only mode");
}

// Initialize and start WiFi AP
static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID:%s Password:%s Channel:%d",
             WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL);
    ESP_LOGI(TAG, "Connect to http://192.168.4.1 to configure");
    ESP_LOGI(TAG, "AP will automatically turn off in %d minutes", AP_TIMEOUT_MINUTES);
    
    // Start web server
    server = start_webserver();
    ap_active = true;
    
    // Create and start timer
    ap_timer = xTimerCreate("ap_timer", pdMS_TO_TICKS(AP_TIMEOUT_MS), pdFALSE, NULL, ap_timer_callback);
    if (ap_timer != NULL) {
        xTimerStart(ap_timer, 0);
    }
}

// Task to update random number periodically
static void update_random_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Update every 5 seconds
        
        // Update the random number directly (no lock needed for simple assignment)
        holding_reg_params.random_number = (uint16_t)(esp_random() % 65536);
        
        ESP_LOGI(TAG, "Random number updated: %u", holding_reg_params.random_number);
    }
}

// Task to update uptime statistics
static void uptime_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Update every second
        modbus_stats.uptime_seconds++;
    }
}

// Task to update second counter in register 2
static void second_counter_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Update every second
        
        // Increment counter, reset to 0 when it reaches max value
        if (holding_reg_params.holding_reg[0] >= 65535) {
            holding_reg_params.holding_reg[0] = 0;
            ESP_LOGI(TAG, "Second counter reset to 0");
        } else {
            holding_reg_params.holding_reg[0]++;
        }
    }
}

void app_main(void)
{
    mb_param_info_t reg_info;
    mb_register_area_descriptor_t reg_area;
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Load configuration
    load_config();
    
    // Set log level
    esp_log_level_set(TAG, ESP_LOG_INFO);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-S3 Modbus RTU Slave with HW-519");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Slave Address: %d", configured_slave_addr);
    ESP_LOGI(TAG, "Baudrate: %d", MB_DEV_SPEED);
    ESP_LOGI(TAG, "UART Port: %d", MB_PORT_NUM);
    ESP_LOGI(TAG, "TX Pin: GPIO%d (HW-519 TXD)", MB_UART_TXD);
    ESP_LOGI(TAG, "RX Pin: GPIO%d (HW-519 RXD)", MB_UART_RXD);
    ESP_LOGI(TAG, "========================================");
    
    // Start WiFi AP for configuration
    ESP_LOGI(TAG, "Starting WiFi AP for configuration...");
    wifi_init_softap();
    
    // Initialize Modbus controller
    void *mbc_slave_handler = NULL;
    
    ESP_ERROR_CHECK(mbc_slave_init(MB_PORT_SERIAL_SLAVE, &mbc_slave_handler));
    
    mb_communication_info_t comm_info = { 0 };
    comm_info.port = MB_PORT_NUM;
    comm_info.mode = MB_MODE_RTU;
    comm_info.baudrate = MB_DEV_SPEED;
    comm_info.parity = MB_PARITY_NONE;
    comm_info.slave_addr = configured_slave_addr;  // Use configured slave address
    
    ESP_ERROR_CHECK(mbc_slave_setup(&comm_info));
    
    // The code below initializes Modbus register area descriptors
    // for Modbus Holding Registers
    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = MB_REG_HOLDING_START;
    reg_area.address = (void*)&holding_reg_params;
    reg_area.size = sizeof(holding_reg_params);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    
    // Initialize register values
    setup_reg_data();
    
    // Start Modbus stack (this initializes UART)
    ESP_ERROR_CHECK(mbc_slave_start());
    
    // Set UART pin numbers (must be done after mbc_slave_start)
    ESP_ERROR_CHECK(uart_set_pin(MB_PORT_NUM, MB_UART_TXD, MB_UART_RXD, 
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    // Use RS485 half-duplex mode with collision detection
    // This mode adds timing that might help with automatic switching
    ESP_ERROR_CHECK(uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_COLLISION_DETECT));
    
    ESP_LOGI(TAG, "UART RS485 collision detect mode configured");
    
    // Verify UART configuration
    ESP_LOGI(TAG, "Verifying UART configuration...");
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uart_get_word_length(MB_PORT_NUM, &data_bits);
    uart_get_parity(MB_PORT_NUM, &parity);
    uart_get_stop_bits(MB_PORT_NUM, &stop_bits);
    ESP_LOGI(TAG, "UART Config - Data bits: %d, Parity: %d, Stop bits: %d", 
             data_bits, parity, stop_bits);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Modbus slave stack initialized successfully");
    ESP_LOGI(TAG, "RESPONDING ONLY TO SLAVE ADDRESS: %d", configured_slave_addr);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Modbus registers:");
    ESP_LOGI(TAG, "  Address 0: Sequential Counter (Read/Write)");
    ESP_LOGI(TAG, "  Address 1: Random Number (Read Only)");
    ESP_LOGI(TAG, "  Address 2: Second Counter (Read Only)");
    ESP_LOGI(TAG, "Waiting for Modbus master requests...");
    
    // Create task to update random number
    xTaskCreate(update_random_task, "update_random", 2048, NULL, 5, NULL);
    
    // Create task to update uptime
    xTaskCreate(uptime_task, "uptime", 2048, NULL, 5, NULL);
    
    // Create task to update second counter in register 2
    xTaskCreate(second_counter_task, "second_counter", 2048, NULL, 5, NULL);
    
    // Check UART buffer to see if data is arriving
    size_t uart_buf_len;
    uart_get_buffered_data_len(MB_PORT_NUM, &uart_buf_len);
    ESP_LOGI(TAG, "UART buffer at startup: %d bytes", uart_buf_len);
    
    // Main event loop
    ESP_LOGI(TAG, "Entering main Modbus polling loop...");
    uint32_t poll_count = 0;
    uint32_t last_uart_check = 0;
    uint32_t last_status_log = 0;
    
    while (1) {
        // Check UART buffer every 2 seconds to see if data is arriving
        if (poll_count - last_uart_check > 200) {
            uart_get_buffered_data_len(MB_PORT_NUM, &uart_buf_len);
            if (uart_buf_len > 0) {
                ESP_LOGW(TAG, "!!! UART has %d bytes in buffer - data is arriving !!!", uart_buf_len);
                // Try to peek at the raw data
                uint8_t peek_buf[32];
                size_t read_len = (uart_buf_len < sizeof(peek_buf)) ? uart_buf_len : sizeof(peek_buf);
                int peek_len = uart_read_bytes(MB_PORT_NUM, peek_buf, read_len, 0);
                if (peek_len > 0) {
                    ESP_LOGW(TAG, "Raw UART data received:");
                    ESP_LOG_BUFFER_HEX_LEVEL(TAG, peek_buf, peek_len, ESP_LOG_WARN);
                }
            }
            last_uart_check = poll_count;
        }
        
        // Periodic status output every 5 seconds
        if (poll_count - last_status_log > 500) {
            ESP_LOGI(TAG, "Alive - Requests: %lu, Reads: %lu, Writes: %lu", 
                     modbus_stats.total_requests, 
                     modbus_stats.read_requests, 
                     modbus_stats.write_requests);
            last_status_log = poll_count;
        }
        
        poll_count++;
        
        // Check for Modbus events
        mb_event_group_t event = mbc_slave_check_event(MB_READ_WRITE_MASK);
        
        if (event != MB_EVENT_NO_EVENTS) {
            ESP_LOGI(TAG, "=== Modbus Event Detected! Event: 0x%02x ===", (unsigned)event);
            ESP_LOGI(TAG, "Current configured slave address: %d", configured_slave_addr);
            // Get parameter information
            ESP_ERROR_CHECK_WITHOUT_ABORT(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT));
            
            const char* rw_str = (reg_info.type & MB_READ_MASK) ? "READ" : "WRITE";
            
            if (reg_info.type & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) {
                // Update statistics
                modbus_stats.total_requests++;
                if (reg_info.type & MB_READ_MASK) {
                    modbus_stats.read_requests++;
                } else {
                    modbus_stats.write_requests++;
                }
                
                // Increment sequential counter on each access to register 0
                if (reg_info.mb_offset == 0) {
                    holding_reg_params.sequential_counter++;
                    ESP_LOGI(TAG, "Sequential counter incremented to: %u", 
                             holding_reg_params.sequential_counter);
                }
                
                ESP_LOGI(TAG, "HOLDING %s: Addr=%u, Size=%u, Value[0]=%u, Value[1]=%u",
                         rw_str,
                         (unsigned)reg_info.mb_offset,
                         (unsigned)reg_info.size,
                         holding_reg_params.sequential_counter,
                         holding_reg_params.random_number);
                
                // Check if response was actually sent
                ESP_LOGI(TAG, "Response should have been sent on GPIO17 (TX)");
                
                // Read a few bytes from UART to see if there's raw data we can log
                uint8_t peek_buf[32];
                int peek_len = uart_read_bytes(MB_PORT_NUM, peek_buf, sizeof(peek_buf), 0);
                if (peek_len > 0) {
                    ESP_LOG_BUFFER_HEX(TAG, peek_buf, peek_len);
                }
            }
        }
        
        // Small delay to prevent task watchdog
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Cleanup (never reached in this example)
    mbc_slave_destroy();
}
