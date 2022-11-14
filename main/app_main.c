#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_sntp.h>
#include "azure_c_shared_utility/platform.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "esp_spi_flash.h"

#include <wifi_provisioning/manager.h>
#include "driver/gpio.h"
#include "lwip/apps/sntp.h"

#include "driver/uart.h"
#include "string.h"
#include "esp_log.h"
#include "cJSON.h"

#include "cloud_manager.h"
#include "wifi_prov.h"

#ifdef CONFIG_EXAMPLE_PROV_TRANSPORT_SOFTAP
#include <wifi_provisioning/scheme_softap.h>
#endif /* CONFIG_EXAMPLE_PROV_TRANSPORT_SOFTAP */

static const char *TAG = "main";
#define Wifi_Led 25

// Azure IoT Cloud handle declaration
TaskHandle_t cloud_manager_handle;
TaskHandle_t alert_manager_handle;

// Azure IoT Cloud task function definition
void azure_task(void *pvParameter)
{
    ESP_LOGI(TAG, "AZURE TASK");

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, false, true, portMAX_DELAY);

    ESP_LOGI(TAG, "Connected to AP success!");

    iothub_client_sample_mqtt_run();

    vTaskDelete(NULL);
}

void app_main(void)
{
    // nvs_flash_erase();
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loops */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();
    endpoint_event_group = xEventGroupCreate();
    // cloud_event_group = xEventGroupCreate();
    alert_event_group_init();

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config();

    prov_init();
    

    /* If device is not yet provisioned start provisioning service */

    xTaskCreate(Wifi_Task, "Wifi_Task", 8 * 1024, NULL, 1, &wifi_task_handle);

    /* Wait for Wi-Fi connection */
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, false, true, portMAX_DELAY);

    init();
    xTaskCreate(rx_task, "uart_rx_task", 1024 * 5, NULL, tskIDLE_PRIORITY, NULL);

    

    gpio_pad_select_gpio(Wifi_Led);

    gpio_set_direction(Wifi_Led, GPIO_MODE_OUTPUT);

    unsigned int bit_values;
    bit_values = xEventGroupGetBits(wifi_event_group);

    if ((bit_values & WIFI_CONNECTED_EVENT) == WIFI_CONNECTED_EVENT)
    {
        gpio_set_level(Wifi_Led, 1);
    }

    xEventGroupWaitBits(cloud_event_group, DATA_RECIEVED_CREATED_EVENT, false, true, portMAX_DELAY);

    nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, "CS_Config", NVS_TYPE_STR);

    if (it != NULL)
    {
        get_nvs_string();

        while (saved_connection_String[0] == '\0')
        {
            xEventGroupWaitBits(endpoint_event_group, ENDPOINT_CREATED_EVENT, true, true, portMAX_DELAY);
            set_nvs_string();
            get_nvs_string();
        }

        ESP_LOGI(TAG, "Connection String1: %s", saved_connection_String);
        cloud_init();
        if (xTaskCreate(&azure_task, "azure_task", 1024 * 8, NULL, 2, &cloud_manager_handle) != pdPASS)
        {
            printf("Azure task create failed\r\n");
        }
        else
        {
            printf("Azure task successfully created\r\n");
        }

        if (xTaskCreate(&send_alert_to_cloud, "send_alert_to_cloud", 1024 * 8, NULL, 2, NULL) != pdPASS)
        {
            printf("send_alert_to_cloud task create failed\r\n");
        }
        else
        {
            printf("send_alert_to_cloud task successfully created\r\n");
        }

        xEventGroupWaitBits(endpoint_event_group, ENDPOINT_CREATED_EVENT, true, true, portMAX_DELAY);

        vTaskDelete(cloud_manager_handle);
        printf("Azure task successfully deleted\r\n");
        sntp_stop();
        set_nvs_string();
        get_nvs_string();

        while (saved_connection_String[0] == '\0')
        {
            xEventGroupWaitBits(endpoint_event_group, ENDPOINT_CREATED_EVENT, true, true, portMAX_DELAY);
            set_nvs_string();
            get_nvs_string();
        }

        if (xTaskCreate(&azure_task, "azure_task", 1024 * 8, NULL, 2, NULL) != pdPASS)
        {
            printf("Azure task create failed\r\n");
        }
        else
        {
            printf("Azure task successfully created\r\n");
        }
    }
    else
    {
        xEventGroupWaitBits(endpoint_event_group, ENDPOINT_CREATED_EVENT, true, true, portMAX_DELAY);

        set_nvs_string();
        get_nvs_string();

        while (saved_connection_String[0] == '\0')
        {
            xEventGroupWaitBits(endpoint_event_group, ENDPOINT_CREATED_EVENT, true, true, portMAX_DELAY);
            set_nvs_string();
            get_nvs_string();
        }

        if (xTaskCreate(&azure_task, "azure_task", 1024 * 8, NULL, 2, NULL) != pdPASS)
        {
            printf("Azure task create failed\r\n");
        }
        else
        {
            printf("Azure task successfully created\r\n");
        }
    }

    // if (xTaskCreate(&alert_func, "Alert_Func", 1024 * 8, NULL, 3, &alert_manager_handle) != pdPASS)
    // {
    //     printf("Alert task create failed\r\n");
    // }
    // else
    // {
    //     printf("Alert task successfully created\r\n");
    //     xEventGroupWaitBits(alert_event_group, ALERT_RECIEVED_CREATED_EVENT, true, true, portMAX_DELAY);

    //     vTaskDelete(cloud_manager_handle);
    //     printf("Azure task successfully deleted\r\n");
    //     sntp_stop();

    //     if (xTaskCreate(&azure_task, "azure_task", 1024 * 8, NULL, 2, cloud_manager_handle) != pdPASS)
    //     {
    //         printf("Azure task create failed\r\n");
    //     }
    //     else
    //     {
    //         printf("Azure task successfully created\r\n");
    //     }
    // }
}