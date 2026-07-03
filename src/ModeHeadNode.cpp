#include "ModeHeadNode.h"

#include <Arduino.h>

#include "Hardware.h"
#include "MqttManager.h"

namespace {

void serviceHeadNodeNodeRole()
{
  // Reserved for local hardware command handling.
}

void serviceHeadNodeGatewayRole()
{
  // Reserved for MQTT <-> CAN forwarding logic.
}

} // namespace

void headNodeSetup()
{
  initBoard();
  initHeadNodeMqtt();
}

void headNodeLoop()
{
  serviceHeadNodeNodeRole();
  serviceHeadNodeGatewayRole();
  serviceHeadNodeMqtt();
}