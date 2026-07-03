#pragma once

#include <Arduino.h>

void initHeadNodeMqtt();
void serviceHeadNodeMqtt();
bool isHeadNodeMqttConnected();
bool publishHeadNodeTestMessage(const char *payload);