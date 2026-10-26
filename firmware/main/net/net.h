// Wi-Fi (through the ESP32-C6), clock sync, and the WebSocket to the relay.
#pragma once

/** Start Wi-Fi; once online, sync the clock and connect to the relay. */
void net_start(void);
/** Queue a JSON message for the relay (safe to call from the UI task). */
void net_send(const char *json);
