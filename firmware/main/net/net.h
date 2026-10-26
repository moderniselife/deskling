// Wi-Fi (through the ESP32-C6), clock sync, mDNS, the fallback hotspot and the relay WebSocket.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui.h"

/** Start networking with the saved settings. Shows setup on screen if nothing is configured. */
void net_start(void);
/** Queue a JSON message for the relay (safe to call from the UI task). */
void net_send(const char *json);

// Requests from the UI or the web page. All return immediately; work happens on the net task.
void net_request_scan(void);
void net_request_wifi(const char *ssid, const char *password);
void net_request_relay(const char *url, const char *token);

bool net_wifi_connected(void);
bool net_relay_online(void);
const char *net_ip(void);
const char *net_code(void);
const char *net_ap_ssid(void);
/** The last Wi-Fi scan, strongest first. */
int net_scan_results(ui_wifi_ap_t *out, int max);
/** Rebuild the device info card on screen (call after any settings change). */
void net_push_info(void);
