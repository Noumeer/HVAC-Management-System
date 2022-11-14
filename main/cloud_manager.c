// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdio.h>
#include <stdlib.h>

#include "iothub_client.h"
#include "iothub_device_client_ll.h"
#include "iothub_client_options.h"
#include "iothub_message.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/platform.h"
#include "azure_c_shared_utility/shared_util_options.h"
#include "iothubtransportmqtt.h"
#include "iothub_client_options.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_prov.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <freertos/queue.h>

#include <wifi_provisioning/manager.h>
#include "cloud_manager.h"
#include "driver/gpio.h"
#include "lwip/apps/sntp.h"
#include <freertos/event_groups.h>

#include "driver/uart.h"
#include "string.h"
#include "esp_log.h"
#include "cJSON.h"
#include "uuid.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

static const char *TAG = "Cloud_Manager";


EventGroupHandle_t cloud_event_group = NULL;
const int DATA_RECIEVED_CREATED_EVENT = BIT0;
const int ALERT_RECIEVED_CREATED_EVENT = BIT1;
const int CONNECTED_TO_CLOUD_EVENT = BIT2;

static SemaphoreHandle_t cloud_mutex;
IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle;

// Uart buffer size declarations
static const int UART_BUF_SIZE = 1024;
#define TXD_PIN (GPIO_NUM_1)
#define RXD_PIN (GPIO_NUM_3)

// GPIOs Pin declaration
#define Wifi_Led 25
#define Pan_Flood_Sensor 34
#define Battery 33
#define AC_Relay 32
static esp_adc_cal_characteristics_t adc1_chars;

// cloud data variables declaration
char MessageId[UUID_STR_LEN];
uint8_t esp32_mac[6];
char Firmware[20];
char TimeStamp[128];
char *Temp_data = "0";
char *RH_data = "0";
char *BMP_data = "0";
char *CATLock_data = "false";
char *WaterLevel_data = "-1";
char CartPanFlooded_str[10];
char RelayOn[10];
int Wifi_rssi;
int BattLevel;
char Malfunction[15] = "false";
char Message[100];
bool mqtt_connection_established = false;
int iterator = 0;

struct tm timeinfo;
int CartPanFlooded;



#ifdef MBED_BUILD_TIMESTAMP
#define SET_TRUSTED_CERT_IN_SAMPLES
#endif // MBED_BUILD_TIMESTAMP

#ifdef SET_TRUSTED_CERT_IN_SAMPLES
#include "certs.h"
#endif // SET_TRUSTED_CERT_IN_SAMPLES

/*String containing Hostname, Device Id & Device Key in the format:                         */
/*  "HostName=<host_name>;DeviceId=<device_id>;SharedAccessKey=<device_key>"                */
/*  "HostName=<host_name>;DeviceId=<device_id>;SharedAccessSignature=<device_sas_token>"    */
// #define EXAMPLE_IOTHUB_CONNECTION_STRING CONFIG_IOTHUB_CONNECTION_STRING
// static const char* connectionString = EXAMPLE_IOTHUB_CONNECTION_STRING;

static int callbackCounter;
static char msgText[1024];
// static char propText[1024];
static bool g_continueRunning;
#define MESSAGE_COUNT CONFIG_MESSAGE_COUNT
#define DOWORK_LOOP_NUM 3

typedef struct EVENT_INSTANCE_TAG
{
    IOTHUB_MESSAGE_HANDLE messageHandle;
    size_t messageTrackingId; // For tracking the messages within the user callback.
} EVENT_INSTANCE;
void alert_event_group_init()
{
    cloud_event_group = xEventGroupCreate();
    cloud_mutex = xSemaphoreCreateMutex();
}
void init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_0, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void rx_task(void *pvParam)
{
    const cJSON *Water_Level = NULL;
    const cJSON *Temperature = NULL;
    const cJSON *Hum = NULL;
    const cJSON *Pressure = NULL;
    const cJSON *CAT_Lock = NULL;
    bool alert_state = false;
    bool send_notification_to_cloud = false;
    // ssize_t in_data_len;

    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    char *data = (char *)malloc(UART_BUF_SIZE + 1);

    while (1)
    {
        const int rxBytes = uart_read_bytes(UART_NUM_0, data, UART_BUF_SIZE, 500 / portTICK_RATE_MS);
        if (rxBytes > 0)
        {
            data[rxBytes] = 0;
            // ESP_LOGI(RX_TASK_TAG, "Read data: %s", data);
        }

        cJSON *data_json = cJSON_Parse(data);
        if (data_json == NULL)
        {
            const char *error_ptr = cJSON_GetErrorPtr();
            if (error_ptr != NULL)
            {
                // fprintf(stderr, "Error before: %s\n", error_ptr);
                // fprintf(stderr, "Error before: %s\n", error_ptr);
                strcpy(Malfunction, "true");
                strcpy(Message, "Unable to get data from Daughter board");
            }
        }

        // MessagId Generation
        uuid_t GUID;
        uuid_generate(GUID);
        uuid_unparse(GUID, MessageId);
        printf("{\n\"ccMessageId\": \"%s\", \n", MessageId);

        // Wifi MACId Generation
        esp_wifi_get_mac(ESP_IF_WIFI_STA, esp32_mac);
        printf("\"ccMACId\": \"%02X:%02X:%02X:%02X:%02X:%02X\", \n", esp32_mac[0], esp32_mac[1], esp32_mac[2], esp32_mac[3], esp32_mac[4],
               esp32_mac[5]);

        // Firmware version number
        strcpy(Firmware, "v-03 Build No. 3");
        printf("\"ccFirmware\": \"%s\",\n", Firmware);

        // Time stamp
        time_t now;
        time(&now);
        // Set timezone to Pakistan Standard Time
        // setenv("TZ", "UTC,M3.2.0/2,M11.1.0", 1);
        // tzset();

        localtime_r(&now, &timeinfo);
        strftime(TimeStamp, sizeof(TimeStamp), "%c", &timeinfo);
        // printf("\"ccTimeStamp\": \"%s\",\n", TimeStamp);
        // printf("Isdst: %d\n", timeinfo.tm_mon);
        printf("TimeStamp: \"%d-%02d-%02dT%02d:%02d:%02dZ\" \n", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        // Temperature data from daughter board parsing
        Temperature = cJSON_GetObjectItemCaseSensitive(data_json, "Temp");
        if (cJSON_IsString(Temperature) && (Temperature->valuestring != NULL))
        {
            Temp_data = Temperature->valuestring;
            printf("\"ccTempC\": \"%s\",\n", Temp_data);
        }

        // Humidity data from daughter board parsing
        Hum = cJSON_GetObjectItemCaseSensitive(data_json, "RH");
        if (cJSON_IsString(Hum) && (Hum->valuestring != NULL))
        {
            RH_data = Hum->valuestring;
            printf("\"ccRH: \"%s\",\n", RH_data);
        }

        // Pressure data from daughter board parsing
        Pressure = cJSON_GetObjectItemCaseSensitive(data_json, "BMP");
        if (cJSON_IsString(Pressure) && (Pressure->valuestring != NULL))
        {
            BMP_data = Pressure->valuestring;
            printf("\"ccBMP\": \"%s\",\n", BMP_data);
        }

        // CATLock data from daughter board parsing
        CAT_Lock = cJSON_GetObjectItemCaseSensitive(data_json, "CATLock");
        if (cJSON_IsString(CAT_Lock) && (CAT_Lock->valuestring != NULL))
        {
            CATLock_data = CAT_Lock->valuestring;
            printf("\"ccCartLock\": \"%s\",\n", CATLock_data);
        }

        // Water_Level data from daughter board parsing
        Water_Level = cJSON_GetObjectItemCaseSensitive(data_json, "WaterLevel");

        if (cJSON_IsString(Water_Level) && (Water_Level->valuestring != NULL))
        {
            WaterLevel_data = Water_Level->valuestring;
            printf("\"ccWaterLevel\": \"%s\",\n", WaterLevel_data);
        }

        // Pan Flood Level
        gpio_pad_select_gpio(Pan_Flood_Sensor);
        gpio_set_direction(Pan_Flood_Sensor, GPIO_MODE_INPUT);
        CartPanFlooded = gpio_get_level(Pan_Flood_Sensor);
        if (CartPanFlooded)
        {
            strcpy(CartPanFlooded_str, "true");
        }
        else
        {
            strcpy(CartPanFlooded_str, "false");
        }
        printf("\"ccCartPanFlooded\": \"%s\", \n", CartPanFlooded_str);

        // Relay level
        gpio_pad_select_gpio(AC_Relay);
        gpio_set_direction(AC_Relay, GPIO_MODE_OUTPUT);
        if (CartPanFlooded == 1)
        {
            gpio_set_level(AC_Relay, 1);
            strcpy(RelayOn, "true");
        }
        else
        {
            gpio_set_level(AC_Relay, 0);
            strcpy(RelayOn, "false");
        }
        printf("\"ccRelayOn\": \"%s\", \n", RelayOn);

        // Wifi Rssi
        wifi_ap_record_t ap;
        esp_wifi_sta_get_ap_info(&ap);
        Wifi_rssi = (100 + ap.rssi);
        printf("\"ccWifi\": \"%d\"\n", Wifi_rssi);

        // Battery level Information
        int reading;
        // gpio_pad_select_gpio(Battery);
        // gpio_set_direction(Battery, GPIO_MODE_INPUT);
        // gpio_set_pull_mode(Battery, GPIO_PULLUP_PULLDOWN);

        esp_err_t ret;
        bool cali_enable = false;

        ret = esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF);
        if (ret == ESP_ERR_NOT_SUPPORTED)
        {
            ESP_LOGW(TAG, "Calibration scheme not supported, skip software calibration");
        }
        else if (ret == ESP_ERR_INVALID_VERSION)
        {
            ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
        }
        else if (ret == ESP_OK)
        {
            cali_enable = true;
            esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11 , ADC_WIDTH_BIT_12, 3300, &adc1_chars);
        }
        else
        {
            ESP_LOGE(TAG, "Invalid arg");
        }

        int voltage = 0;

        ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
        ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11));

        // voltage = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_5), &adc1_chars);
        reading = adc1_get_raw(ADC1_CHANNEL_5);
        // printf("RAW value: %d \n", reading);
        if (cali_enable)
        {
            voltage = esp_adc_cal_raw_to_voltage(reading, &adc1_chars);
            // printf("Cal. Value: %d mV \n", voltage);
        }
        BattLevel = ((voltage * 100) / 3300);
        printf("\"ccBattLevel\": \"%d\", \n", BattLevel);

        // Malfunction information
        printf("\"ccMalfunction\": \"%s\",\n", Malfunction);

        // Message to send alerts from device to cloud to show if there is a malfunction
        printf("\"ccMessage\": \"%s\"\n}\n", Message);

        // printf("{\n\"MessageId\" : %s,\n\"MACId\" : %02X:%02X:%02X:%02X:%02X:%02X,\n\"Firmware\" : \"V-03 Build No. 3\",\n\"TimeStamp\" : %s,\n\"TempC\" : %s,\n\"RH\" : %s,\n\"BMP\" : %s,\n\"CartLock\" : %s,\n\"WaterLevel\" : %s,\n\"CartPanFlooded\" : %d,\n\"RelayOn\" : %d,\n\"Wifi\" : %d,\n\"BattLevel\" : %.2f,\n\"Malfunction\" : %d,\n\"Message\" : %s,\n\"Sensor1Data\" : \"\",\n\"Sensor2Data\" : \"\",\n\"Sensor3Data\" : \"\"\n}\n", MessageId, esp32_mac[0], esp32_mac[1], esp32_mac[2], esp32_mac[3], esp32_mac[4], esp32_mac[5], TimeStamp, Temp, RH, BMP, CATLock, WaterLevel, CartPanFlooded, RelayOn, -(ap.rssi), BattLevel, Malfunction, Message);

        // Reset values at the end of loop
        strcpy(Malfunction, "false");
        strcpy(Message, "");
        vTaskDelay(500 / portTICK_PERIOD_MS);

        // printf("{\n\"ccMessageId\": \"%s\",\n\"ccMACId\": \"%02X:%02X:%02X:%02X:%02X:%02X\",\n\"ccFirmware\": \"v-03 Build No. 3\",\n\"ccTempC\": \"%s\",\n\"ccRH\": \"%s\",\n\"ccBMP\": \"%s\",\n\"CartLock\": \"%s\",\n\"WaterLevel\": \"%s\",\n\"CartPanFlooded\": \"%d\",\n\"RelayOn\": \"%d\",\n\"Wifi\":\"%d\",\n\"BattLevel\": \"%.2f\",\n\"Malfunction\": \"%d\",\n\"Message\": \"%s\",\n\"Sensor1Data\": \"\",\n\"Sensor2Data\": \"\",\n\"Sensor3Data\": \"\",\n}\n", MessageId, esp32_mac[0], esp32_mac[1], esp32_mac[2], esp32_mac[3], esp32_mac[4], esp32_mac[5], Temp, RH, BMP, CATLock, WaterLevel, CartPanFlooded, RelayOn, Wifi_rssi, BattLevel, Malfunction, Message);
        // ,\n\"ccTimeStamp\": \"%s\" TimeStamp,
        xEventGroupSetBits(cloud_event_group, DATA_RECIEVED_CREATED_EVENT);

        if (((atoi(Temp_data)) >= 50) || (strcmp(CATLock_data, "false") == 0) || (((atoi(WaterLevel_data)) <= 0) || ((atoi(WaterLevel_data)) >= 75)) || (strcmp(CartPanFlooded_str, "true") == 0) || (strcmp(RelayOn, "true") == 0) || (Wifi_rssi <= 25) || (BattLevel <= 25))
        {
            printf("\nalert\n");
            if(alert_state == false)
            {
                printf("\nalert1\n");
                alert_state = true;
                send_notification_to_cloud = true;
            }
            
        }
        else
        {
            alert_state = false;
        }

        if(send_notification_to_cloud)
        {
            xEventGroupSetBits(cloud_event_group, ALERT_RECIEVED_CREATED_EVENT);
            printf("\"\n\nALERT_RECIEVED_CREATED_EVENT\n\n\"\n");
            send_notification_to_cloud = false;
        }
    }
    free(data);
}

static IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE message, void *userContextCallback)
{
    int *counter = (int *)userContextCallback;
    const char *buffer;
    size_t size;
    MAP_HANDLE mapProperties;
    const char *messageId;
    const char *correlationId;

    // Message properties
    if ((messageId = IoTHubMessage_GetMessageId(message)) == NULL)
    {
        messageId = "<null>";
    }

    if ((correlationId = IoTHubMessage_GetCorrelationId(message)) == NULL)
    {
        correlationId = "<null>";
    }

    // Message content
    if (IoTHubMessage_GetByteArray(message, (const unsigned char **)&buffer, &size) != IOTHUB_MESSAGE_OK)
    {
        (void)printf("unable to retrieve the message data\r\n");
    }
    else
    {
        (void)printf("Received Message [%d]\r\n Message ID: %s\r\n Correlation ID: %s\r\n Data: <<<%.*s>>> & Size=%d\r\n", *counter, messageId, correlationId, (int)size, buffer, (int)size);
        // If we receive the work 'quit' then we stop running
        if (size == (strlen("quit") * sizeof(char)) && memcmp(buffer, "quit", size) == 0)
        {
            g_continueRunning = false;
        }
    }
    // Retrieve properties from the message
    mapProperties = IoTHubMessage_Properties(message);
    if (mapProperties != NULL)
    {
        const char *const *keys;
        const char *const *values;
        size_t propertyCount = 0;
        if (Map_GetInternals(mapProperties, &keys, &values, &propertyCount) == MAP_OK)
        {
            if (propertyCount > 0)
            {
                size_t index;

                printf(" Message Properties:\r\n");
                for (index = 0; index < propertyCount; index++)
                {
                    (void)printf("\tKey: %s Value: %s\r\n", keys[index], values[index]);
                }
                (void)printf("\r\n");
            }
        }
    }

    /* Some device specific action code goes here... */
    (*counter)++;
    return IOTHUBMESSAGE_ACCEPTED;
}

static void SendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *userContextCallback)
{
    EVENT_INSTANCE *eventInstance = (EVENT_INSTANCE *)userContextCallback;
    size_t id = eventInstance->messageTrackingId;

    if (result == IOTHUB_CLIENT_CONFIRMATION_OK)
    {
        (void)printf("Confirmation[%d] received for message tracking id = %d with result = %s\r\n", callbackCounter, (int)id, MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));
        /* Some device specific action code goes here... */
        callbackCounter++;
    }
    IoTHubMessage_Destroy(eventInstance->messageHandle);
}

void connection_status_callback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void *userContextCallback)
{
    if(result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED )
    {
        mqtt_connection_established = true;
    }
    (void)printf("\n\nConnection Status result:%s, Connection Status reason: %s\n\n", MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS, result),
                 MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS_REASON, reason));
}

void cloud_init()
{
    int receiveContext = 0;
    if (platform_init() != 0)
    {
        (void)printf("Failed to initialize the platform.\r\n");
    }
    else
    {
        if ((iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(saved_connection_String, MQTT_Protocol)) == NULL)
        {
            (void)printf("ERROR: iotHubClientHandle is NULL!\r\n");
        }
        else
        {
            bool traceOn = true;
            if(IoTHubClient_LL_SetOption(iotHubClientHandle, OPTION_LOG_TRACE, &traceOn) == IOTHUB_CLIENT_OK)
            {
                printf("\n\nsetOptions success\n\n");
            }

            if(IoTHubClient_LL_SetConnectionStatusCallback(iotHubClientHandle, connection_status_callback, NULL) == IOTHUB_CLIENT_OK)
            {
                printf("\n\nSetConnectionStatusCallback success\n\n");
            }
            // Setting the Trusted Certificate.  This is only necessary on system with without
            // built in certificate stores.
#ifdef SET_TRUSTED_CERT_IN_SAMPLES
            IoTHubDeviceClient_LL_SetOption(iotHubClientHandle, OPTION_TRUSTED_CERT, certificates);
#endif // SET_TRUSTED_CERT_IN_SAMPLES

            /* Setting Message call back, so we can receive Commands. */
            if (IoTHubClient_LL_SetMessageCallback(iotHubClientHandle, ReceiveMessageCallback, &receiveContext) != IOTHUB_CLIENT_OK)
            {
                (void)printf("ERROR: IoTHubClient_LL_SetMessageCallback..........FAILED!\r\n");
            }
            
            while(mqtt_connection_established == false)
            {
                IoTHubClient_LL_DoWork(iotHubClientHandle);
                ThreadAPI_Sleep(100);
            }

            xEventGroupSetBits(cloud_event_group, CONNECTED_TO_CLOUD_EVENT);
            
        }
    }
    
}

void iothub_client_sample_mqtt_run(void)
{
    EVENT_INSTANCE message;
    g_continueRunning = true;
    srand((unsigned int)time(NULL));

    callbackCounter = 0;
    IOTHUB_CLIENT_STATUS status;


    

//     if (platform_init() != 0)
//     {
//         (void)printf("Failed to initialize the platform.\r\n");
//     }
//     else
//     {
//         if ((iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(saved_connection_String, MQTT_Protocol)) == NULL)
//         {
//             (void)printf("ERROR: iotHubClientHandle is NULL!\r\n");
//         }
//         else
//         {
//             bool traceOn = true;
//             IoTHubClient_LL_SetOption(iotHubClientHandle, OPTION_LOG_TRACE, &traceOn);

//             IoTHubClient_LL_SetConnectionStatusCallback(iotHubClientHandle, connection_status_callback, NULL);
//             // Setting the Trusted Certificate.  This is only necessary on system with without
//             // built in certificate stores.
// #ifdef SET_TRUSTED_CERT_IN_SAMPLES
//             IoTHubDeviceClient_LL_SetOption(iotHubClientHandle, OPTION_TRUSTED_CERT, certificates);
// #endif // SET_TRUSTED_CERT_IN_SAMPLES

//             /* Setting Message call back, so we can receive Commands. */
//             if (IoTHubClient_LL_SetMessageCallback(iotHubClientHandle, ReceiveMessageCallback, &receiveContext) != IOTHUB_CLIENT_OK)
//             {
//                 (void)printf("ERROR: IoTHubClient_LL_SetMessageCallback..........FAILED!\r\n");
//             }
//             else
//             {
//                 (void)printf("IoTHubClient_LL_SetMessageCallback...successful.\r\n");

//                 /* Now that we are ready to receive commands, let's send some messages */
//                 int iterator = 0;
                time_t sent_time = 0;
                time_t current_time = 0;
                xEventGroupWaitBits(cloud_event_group, CONNECTED_TO_CLOUD_EVENT, false, true, portMAX_DELAY);
                
                do
                {
                    //(void)printf("iterator: [%d], callbackCounter: [%d]. \r\n", iterator, callbackCounter);
                    time(&current_time);
                    if(difftime(current_time, sent_time) > 300)//if ((MESSAGE_COUNT == 0 || iterator < MESSAGE_COUNT) && iterator <= callbackCounter && (difftime(current_time, sent_time) > ((CONFIG_MESSAGE_INTERVAL_TIME) / 1000)))
                    {
                        if(xSemaphoreTake(cloud_mutex, portMAX_DELAY) == pdTRUE )
                        {
                            printf("\nsemaphore taken\n");
                            sprintf_s(msgText, sizeof(msgText), "{\n\"\nccMessageId\": \"%s\",\n\"ccMACId\": \"%02X:%02X:%02X:%02X:%02X:%02X\",\n\"ccFirmware\": \"v-03 Build No. 3\",\n\"ccTimeStamp\": \"%d-%02d-%02dT%02d:%02d:%02dZ\",\n\"ccTempC\": \"%s\",\n\"ccRH\": \"%s\",\n\"ccBMP\": \"%s\",\n\"ccCartLock\": \"%s\",\n\"ccWaterLevel\": \"%s\",\n\"ccCartPanFlooded\": \"%s\",\n\"ccRelayOn\": \"%s\",\n\"ccWifi\":\"%02d\",\n\"ccBattLevel\": \"%02d\",\n\"ccMalfunction\": \"%s\",\n\"ccMessage\": \"%s\",\n\"ccSensor1Data\": \"\",\n\"ccSensor2Data\": \"\",\n\"ccSensor3Data\": \"\"\n}\n", MessageId, esp32_mac[0], esp32_mac[1], esp32_mac[2], esp32_mac[3], esp32_mac[4], esp32_mac[5], timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, Temp_data, RH_data, BMP_data, CATLock_data, WaterLevel_data, CartPanFlooded_str, RelayOn, Wifi_rssi, BattLevel, Malfunction, Message);
                            // printf("{\n\"ccMessageId\": \"%s\",\n\"ccMACId\": \"%02X:%02X:%02X:%02X:%02X:%02X\",\n\"ccFirmware\": \"v-03 Build No. 3\",\n\"ccTimeStamp\": \"%s\",\n\"ccTempC\": \"%s\",\n\"ccRH\": \"%s\",\n\"ccBMP\": \"%s\",\n\"ccCartLock\": \"%s\",\n\"ccWaterLevel\": \"%s\",\n\"ccCartPanFlooded\": \"%s\",\n\"ccRelayOn\": \"%s\",\n\"ccWifi\":\"%d\",\n\"ccBattLevel\": \"%.2f\",\n\"ccMalfunction\": \"%s\",\n\"ccMessage\": \"%s\",\n\"ccSensor1Data\": \"\",\n\"ccSensor2Data\": \"\",\n\"ccSensor3Data\": \"\"\n}\n", MessageId, esp32_mac[0], esp32_mac[1], esp32_mac[2], esp32_mac[3], esp32_mac[4], esp32_mac[5], TimeStamp, Temp, RH, BMP, CATLock, WaterLevel, CartPanFlooded_str, RelayOn, Wifi_rssi, BattLevel, Malfunction, Message);
                            if ((message.messageHandle = IoTHubMessage_CreateFromByteArray((const unsigned char *)msgText, strlen(msgText))) == NULL)
                            {
                                (void)printf("ERROR: iotHubMessageHandle is NULL!\r\n");
                            }
                            else
                            {
                                message.messageTrackingId = iterator;
                                // MAP_HANDLE propMap = IoTHubMessage_Properties(message.messageHandle);
                                IoTHubMessage_Properties(message.messageHandle);
                                // (void)sprintf_s(propText, sizeof(propText), temperature > 28 ? "true" : "false");
                                // if (Map_AddOrUpdate(propMap, "temperatureAlert", propText) != MAP_OK)
                                // {
                                //     (void)printf("ERROR: Map_AddOrUpdate Failed!\r\n");
                                // }

                                if (IoTHubClient_LL_SendEventAsync(iotHubClientHandle, message.messageHandle, SendConfirmationCallback, &message) != IOTHUB_CLIENT_OK)
                                {
                                    (void)printf("ERROR: IoTHubClient_LL_SendEventAsync..........FAILED!\r\n");
                                }
                                else
                                {
                                    time(&sent_time);
                                    (void)printf("IoTHubClient_LL_SendEventAsync accepted message [%d] for transmission to IoT Hub.\r\n", (int)iterator);
                                }
                                iterator++;
                            }
                            while ((IoTHubClient_LL_GetSendStatus(iotHubClientHandle, &status) == IOTHUB_CLIENT_OK) && (status == IOTHUB_CLIENT_SEND_STATUS_BUSY))
                            {
                                IoTHubClient_LL_DoWork(iotHubClientHandle);
                                ThreadAPI_Sleep(100);
                            }
                            // printf("\n\nGetSendStatus success\n\n");
                            // IoTHubClient_LL_DoWork(iotHubClientHandle);
                            // ThreadAPI_Sleep(10);
                            xSemaphoreGive(cloud_mutex);
                            printf("\nsemaphore released\n");                
                        }
                        
                        
                        // if (MESSAGE_COUNT != 0 && callbackCounter >= MESSAGE_COUNT)
                        // {
                        //     printf("exit\n");
                        //     break;
                        // }
                        
                    }
                    vTaskDelay(1000 / portTICK_PERIOD_MS);

                }while (g_continueRunning);
                

        //         (void)printf("iothub_client_sample_mqtt has gotten quit message, call DoWork %d more time to complete final sending...\r\n", DOWORK_LOOP_NUM);
        //         size_t index = 0;
        //         for (index = 0; index < DOWORK_LOOP_NUM; index++)
        //         {
        //             IoTHubClient_LL_DoWork(iotHubClientHandle);
        //             ThreadAPI_Sleep(1);
        //         }
        //     }
        //     IoTHubClient_LL_Destroy(iotHubClientHandle);
        // }
        // platform_deinit();
    // }
}

void send_alert_to_cloud(void * param)
{
    EVENT_INSTANCE message;

    g_continueRunning = true;
    srand((unsigned int)time(NULL));

    callbackCounter = 0;
    IOTHUB_CLIENT_STATUS status;

    
    
    time_t sent_time = 0;
    time_t current_time = 0;
    for(;;)
    {
        xEventGroupWaitBits(cloud_event_group, CONNECTED_TO_CLOUD_EVENT, false, true, portMAX_DELAY);
        xEventGroupWaitBits(cloud_event_group, ALERT_RECIEVED_CREATED_EVENT, true, true, portMAX_DELAY);
        time(&current_time);
        sprintf_s(msgText, sizeof(msgText), "{\n\"\nccMessageId\": \"%s\",\n\"ccMACId\": \"%02X:%02X:%02X:%02X:%02X:%02X\",\n\"ccFirmware\": \"v-03 Build No. 3\",\n\"ccTimeStamp\": \"%d-%02d-%02dT%02d:%02d:%02dZ\",\n\"ccTempC\": \"%s\",\n\"ccRH\": \"%s\",\n\"ccBMP\": \"%s\",\n\"ccCartLock\": \"%s\",\n\"ccWaterLevel\": \"%s\",\n\"ccCartPanFlooded\": \"%s\",\n\"ccRelayOn\": \"%s\",\n\"ccWifi\":\"%02d\",\n\"ccBattLevel\": \"%02d\",\n\"ccMalfunction\": \"%s\",\n\"ccMessage\": \"%s\",\n\"ccSensor1Data\": \"\",\n\"ccSensor2Data\": \"\",\n\"ccSensor3Data\": \"\"\n}\n", MessageId, esp32_mac[0], esp32_mac[1], esp32_mac[2], esp32_mac[3], esp32_mac[4], esp32_mac[5], timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, Temp_data, RH_data, BMP_data, CATLock_data, WaterLevel_data, CartPanFlooded_str, RelayOn, Wifi_rssi, BattLevel, Malfunction, Message);
        // printf("{\n\"ccMessageId\": \"%s\",\n\"ccMACId\": \"%02X:%02X:%02X:%02X:%02X:%02X\",\n\"ccFirmware\": \"v-03 Build No. 3\",\n\"ccTimeStamp\": \"%s\",\n\"ccTempC\": \"%s\",\n\"ccRH\": \"%s\",\n\"ccBMP\": \"%s\",\n\"ccCartLock\": \"%s\",\n\"ccWaterLevel\": \"%s\",\n\"ccCartPanFlooded\": \"%s\",\n\"ccRelayOn\": \"%s\",\n\"ccWifi\":\"%d\",\n\"ccBattLevel\": \"%.2f\",\n\"ccMalfunction\": \"%s\",\n\"ccMessage\": \"%s\",\n\"ccSensor1Data\": \"\",\n\"ccSensor2Data\": \"\",\n\"ccSensor3Data\": \"\"\n}\n", MessageId, esp32_mac[0], esp32_mac[1], esp32_mac[2], esp32_mac[3], esp32_mac[4], esp32_mac[5], TimeStamp, Temp, RH, BMP, CATLock, WaterLevel, CartPanFlooded_str, RelayOn, Wifi_rssi, BattLevel, Malfunction, Message);
        printf("\n\nhere\n\n");
        if(xSemaphoreTake(cloud_mutex, portMAX_DELAY) == pdTRUE )
        {
            printf("\n\nhere1\n\n");
            if ((message.messageHandle = IoTHubMessage_CreateFromByteArray((const unsigned char *)msgText, strlen(msgText))) == NULL)
            {
                (void)printf("ERROR: iotHubMessageHandle is NULL!\r\n");
            }
            else
            {
                message.messageTrackingId = iterator;
                // MAP_HANDLE propMap = IoTHubMessage_Properties(message.messageHandle);
                IoTHubMessage_Properties(message.messageHandle);
                // (void)sprintf_s(propText, sizeof(propText), temperature > 28 ? "true" : "false");
                // if (Map_AddOrUpdate(propMap, "temperatureAlert", propText) != MAP_OK)
                // {
                //     (void)printf("ERROR: Map_AddOrUpdate Failed!\r\n");
                // }

                if (IoTHubClient_LL_SendEventAsync(iotHubClientHandle, message.messageHandle, SendConfirmationCallback, &message) != IOTHUB_CLIENT_OK)
                {
                    (void)printf("ERROR: IoTHubClient_LL_SendEventAsync..........FAILED!\r\n");
                }
                else
                {
                    time(&sent_time);
                    (void)printf("IoTHubClient_LL_SendEventAsync accepted message [%d] for transmission to IoT Hub.\r\n", (int)iterator);
                }
                iterator++;
            }
            while ((IoTHubClient_LL_GetSendStatus(iotHubClientHandle, &status) == IOTHUB_CLIENT_OK) && (status == IOTHUB_CLIENT_SEND_STATUS_BUSY))
            {
                IoTHubClient_LL_DoWork(iotHubClientHandle);
                ThreadAPI_Sleep(100);
            }
            printf("\n\nhere2\n\n");
            xSemaphoreGive(cloud_mutex);
        }
        
    }
    
}

int main(void)
{
    iothub_client_sample_mqtt_run();
    return 0;
}
