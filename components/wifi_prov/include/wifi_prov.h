#pragma once

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>


#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#include <wifi_provisioning/manager.h>

// extern char *connectionString;
extern bool endpoint_create;
extern char saved_connection_String [256];

extern const int WIFI_CONNECTED_EVENT;

extern const int ENDPOINT_CREATED_EVENT;

extern EventGroupHandle_t wifi_event_group;

extern EventGroupHandle_t endpoint_event_group;

extern TaskHandle_t wifi_task_handle;

extern bool provisioned;

void get_device_service_name(char *service_name, size_t max);

esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *priv_data);

void set_nvs_string();

void get_nvs_string();

void wifi_config(void);

void prov_init(void);

void Wifi_Task(void *pvParam);

void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// void WifiTimerCallback( TimerHandle_t xTimer );
