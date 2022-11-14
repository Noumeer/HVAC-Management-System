#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "nvs.h"
#include "esp_system.h"

#include <wifi_provisioning/manager.h>
#include "wifi_prov.h"
#include "cJSON.h"

#ifdef CONFIG_EXAMPLE_PROV_TRANSPORT_SOFTAP
#include <wifi_provisioning/scheme_softap.h>
#endif /* CONFIG_EXAMPLE_PROV_TRANSPORT_SOFTAP */

static const char *TAG = "wifi_prov";

bool provisioned = false;
char *connectionString;
char saved_connection_String[256] = "";

// TimerHandle_t wifi_timer = NULL;
TaskHandle_t wifi_task_handle = NULL;
wifi_config_t cred_config;
wifi_config_t wifi_config1;

nvs_handle_t handle;

/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;
const int ENDPOINT_CREATED_EVENT = BIT0;

EventGroupHandle_t wifi_event_group;
EventGroupHandle_t endpoint_event_group = NULL;

void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "eTRAPP_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X:%02X:%02X:%02X:%02X:%02X",
             ssid_prefix, eth_mac[0], eth_mac[1], eth_mac[2], eth_mac[3], eth_mac[4], eth_mac[5]);
}

esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    const cJSON *ssid = NULL;
    const cJSON *password = NULL;
    const cJSON *connection_string = NULL;

    if (inbuf)
    {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }

    // Parsing of JSON
    cJSON *data_json = cJSON_ParseWithLength((char *)inbuf, inlen);
    if (data_json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
    }

    ssid = cJSON_GetObjectItemCaseSensitive(data_json, "ssid");

    if (cJSON_IsString(ssid) && (ssid->valuestring != NULL))
    {
        strcpy((char *)wifi_config1.sta.ssid, ssid->valuestring);
        printf("ssid: \"%s\"\n", wifi_config1.sta.ssid);
    }

    password = cJSON_GetObjectItemCaseSensitive(data_json, "password");
    if (cJSON_IsString(password) && (password->valuestring != NULL))
    {
        strcpy((char *)wifi_config1.sta.password, password->valuestring);
        printf("password: \"%s\"\n", wifi_config1.sta.password);
    }

    connection_string = cJSON_GetObjectItemCaseSensitive(data_json, "connectionString");
    if (cJSON_IsString(connection_string) && (connection_string->valuestring != NULL))
    {
        connectionString = connection_string->valuestring;
        printf("connectionString: \"%s\"\n", connectionString);
    }

    ESP_ERROR_CHECK(wifi_prov_mgr_configure_sta(&wifi_config1));

    char *response = "";

    wifi_prov_sta_state_t state = WIFI_PROV_STA_CONNECTING;

    for (int retry = 0; retry <= 10; retry++)
    {
        unsigned int bit_values;
        if (strlen((const char *)wifi_config1.sta.ssid) != 0)
        {
            ESP_LOGI(TAG, "Connecting to Wifi");
            bit_values = xEventGroupGetBits(wifi_event_group);
            if ((bit_values & WIFI_CONNECTED_EVENT) == WIFI_CONNECTED_EVENT)
            {
                esp_wifi_disconnect();
            }
            ESP_ERROR_CHECK(esp_wifi_start());
            ESP_ERROR_CHECK(esp_wifi_connect());
        }

        ESP_ERROR_CHECK(wifi_prov_mgr_get_wifi_state(&state));
        ESP_LOGI(TAG, "Wifi");

        while (state == WIFI_PROV_STA_CONNECTING)
        {
            ESP_LOGI(TAG, "Wifi Connecting");
            wifi_prov_mgr_get_wifi_state(&state);
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        if (state == WIFI_PROV_STA_CONNECTED)
        {
            ESP_LOGI(TAG, "Wifi Connected");
            response = "SUCCESS";
            break;
        }
        else if (state == WIFI_PROV_STA_DISCONNECTED)
        {
            ESP_LOGI(TAG, "Wifi Disconnected");
            break;
        }
    }

    if (state == WIFI_PROV_STA_DISCONNECTED)
    {
        ESP_ERROR_CHECK(wifi_prov_mgr_reset_sm_state_on_failure());
        response = "FAILED";
    }
    ESP_LOGI(TAG, "Wifi END");

    xEventGroupSetBits(endpoint_event_group, ENDPOINT_CREATED_EVENT);

    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL)
    {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response); /* +1 for NULL terminating byte */

    return ESP_OK;
}

void set_nvs_string()
{
    esp_err_t err = nvs_open("CS_Config", NVS_READWRITE, &handle);

    if (err != ESP_OK)
    {
        printf("FATAL ERROR: Unable to open NVS\n");
    }

    char conn_String[256] = "";

    strcpy(conn_String, connectionString);

    ESP_ERROR_CHECK(nvs_set_str(handle, "Conn_Str", conn_String));

    ESP_ERROR_CHECK(nvs_commit(handle));

    ESP_LOGI(TAG, "Connection String: %s", conn_String);

}
void get_nvs_string()
{
    esp_err_t err = nvs_open("CS_Config", NVS_READWRITE, &handle);

    if (err != ESP_OK)
    {
        printf("FATAL ERROR: Unable to open NVS\n");
    }

    size_t saved_connection_String_len = sizeof(saved_connection_String);

    ESP_ERROR_CHECK(nvs_get_str(handle, "Conn_Str", saved_connection_String, &saved_connection_String_len));

    ESP_LOGI(TAG, "Connection String: %s", saved_connection_String);

}

void wifi_config(void)
{
    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}
void prov_init(void)
{
    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
        /* What is the Provisioning Scheme that we want ?*/
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE};
    /* Initialize provisioning manager with the
     * configuration parameters set above */
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
}

void Wifi_Task(void *pvParam)
{
    // wifi_timer = xTimerCreate("Wifi_Timer", pdMS_TO_TICKS(60000), pdTRUE, (void *)0, WifiTimerCallback);

    /* Let's find out if the device is provisioned */

    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned)
    {

        ESP_LOGI(TAG, "Starting provisioning");

        char service_name[26];

        get_device_service_name(service_name, sizeof(service_name));

        wifi_prov_security_t security = WIFI_PROV_SECURITY_0;

        const char *pop = NULL;

        const char *service_key = "etrapp1234";

        wifi_prov_mgr_endpoint_create("device-configure");

        /* Start provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key));

        wifi_prov_mgr_endpoint_register("device-configure", custom_prov_data_handler, NULL);

        // ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config1));
    }
    else
    {
        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &cred_config));

        if (strlen((const char *)cred_config.sta.ssid))
        {
            ESP_LOGI(TAG, "Found ssid %s", (const char *)cred_config.sta.ssid);
            ESP_LOGI(TAG, "Found password %s", (const char *)cred_config.sta.password);
        }

        ESP_LOGI(TAG, "Starting provisioning");

        char service_name[26];

        get_device_service_name(service_name, sizeof(service_name));

        wifi_prov_security_t security = WIFI_PROV_SECURITY_0;

        const char *pop = NULL;

        const char *service_key = "etrapp1234";

        wifi_prov_mgr_endpoint_create("device-configure");

        /* Start provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key));

        wifi_prov_mgr_endpoint_register("device-configure", custom_prov_data_handler, NULL);

        // ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config1));
    }

    vTaskDelete(NULL);
}
/* Event handler for catching system events */
void event_handler(void *arg, esp_event_base_t event_base,
                   int32_t event_id, void *event_data)
{
#ifdef CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE
    static int retries;
#endif
    if (event_base == WIFI_PROV_EVENT)
    {
        switch (event_id)
        {
        case WIFI_PROV_START:
        {
            ESP_LOGI(TAG, "Provisioning started");

            // xTimerStart(wifi_timer, 0);

            // ESP_LOGI(TAG, "Wifi Timer started");

            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cred_config));
            if (strlen((const char *)cred_config.sta.ssid) != 0)
            {
                ESP_LOGI(TAG, "Connecting to Wifi");
                ESP_ERROR_CHECK(esp_wifi_start());
                ESP_ERROR_CHECK(esp_wifi_connect());
            }
        }
        break;
        case WIFI_PROV_CRED_RECV:
        {
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received Wi-Fi credentials"
                          "\n\tSSID     : %s\n\tPassword : %s",
                     (const char *)wifi_sta_cfg->ssid,
                     (const char *)wifi_sta_cfg->password);
            break;
        }
        case WIFI_PROV_CRED_FAIL:
        {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                          "\n\tPlease reset to factory and retry provisioning",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
#ifdef CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE
            retries++;
            if (retries >= CONFIG_EXAMPLE_PROV_MGR_MAX_RETRY_CNT)
            {
                ESP_LOGI(TAG, "Failed to connect with provisioned AP, reseting provisioned credentials");
                wifi_prov_mgr_reset_sm_state_on_failure();
                retries = 0;
            }
#endif
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            // wifi_prov_mgr_deinit();
#ifdef CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE
            retries = 0;
#endif
            break;
        default:
            break;
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        esp_wifi_connect();
    }
    // else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    // {
    //     ESP_LOGI(TAG, "STA connected to AP");
    //     xTimerStop(wifi_timer, 0);
    //     ESP_LOGI(TAG, "Wifi Timer stopped");
    // }
    // else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    // {
    //     ESP_LOGI(TAG, "STA disconnected from AP");
    //     xTimerReset(wifi_timer, 0);
    //     ESP_LOGI(TAG, "Wifi Timer is Reset");
    // }
}
