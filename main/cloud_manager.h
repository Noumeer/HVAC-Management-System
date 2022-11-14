// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef CLOUD_MANAGER_H
#define CLOUD_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

    /* FreeRTOS event group to signal when we are connected & ready to make a request */

extern EventGroupHandle_t cloud_event_group;
extern const int DATA_RECIEVED_CREATED_EVENT;

    // extern EventGroupHandle_t alert_event_group;
    // extern const int ALERT_RECIEVED_CREATED_EVENT;

    extern char *CATLock_data;
    extern char *WaterLevel_data;
    extern char CartPanFlooded_str[10];
    extern int BattLevel;
    extern int Wifi_rssi;
    extern char RelayOn[10];
    extern char *Temp_data;

void cloud_init();
void alert_event_group_init();
void iothub_client_sample_mqtt_run(void);
void init(void);
void rx_task(void *pvParam);
void alert_func();
void send_alert_to_cloud(void * param);
    // void rec_task (void *pvParam);

#ifdef __cplusplus
}
#endif

#endif /* IOTHUB_CLIENT_SAMPLE_MQTT_H */
