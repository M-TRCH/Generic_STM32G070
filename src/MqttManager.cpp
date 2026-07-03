#include "MqttManager.h"

#include <Ethernet.h>
#include <PubSubClient.h>
#include <SPI.h>

#include "Config.h"

namespace {

EthernetClient ethernetClient;
PubSubClient mqttClient(ethernetClient);

uint32_t lastReconnectAttemptMs = 0;
uint32_t lastPublishMs = 0;

void logMessage(const __FlashStringHelper *message)
{
  Serial.println(message);
}

void onMqttMessage(char *topic, uint8_t *payload, unsigned int length)
{
  Serial.print(F("MQTT RX ["));
  Serial.print(topic);
  Serial.print(F("]: "));

  for (unsigned int index = 0; index < length; ++index) {
    Serial.write(payload[index]);
  }

  Serial.println();
}

void configureSpiForW5500()
{
  SPI.setSCLK(kSpiSckPin);
  SPI.setMISO(kSpiMisoPin);
  SPI.setMOSI(kSpiMosiPin);
  SPI.begin();
}

void startEthernet()
{
  configureSpiForW5500();
  Ethernet.init(kW5500CsPin);

  Serial.println(F("Starting Ethernet (DHCP)..."));
  if (Ethernet.begin(const_cast<uint8_t *>(kHeadNodeMacAddress)) == 0) {
    logMessage(F("Ethernet DHCP failed"));
    return;
  }

  Serial.print(F("Ethernet IP: "));
  Serial.println(Ethernet.localIP());
}

bool ensureMqttConnection()
{
  if (mqttClient.connected()) {
    return true;
  }

  const uint32_t nowMs = millis();
  if (nowMs - lastReconnectAttemptMs < kHeadNodeMqttReconnectDelayMs) {
    return false;
  }

  lastReconnectAttemptMs = nowMs;
  Serial.print(F("Connecting MQTT -> "));
  Serial.print(kHeadNodeMqttBrokerHost);
  Serial.print(F(":"));
  Serial.println(kHeadNodeMqttBrokerPort);

  if (!mqttClient.connect(kHeadNodeMqttClientId)) {
    Serial.print(F("MQTT connect failed, state="));
    Serial.println(mqttClient.state());
    return false;
  }

  if (mqttClient.subscribe(kHeadNodeMqttSubscribeTopic)) {
    Serial.print(F("Subscribed: "));
    Serial.println(kHeadNodeMqttSubscribeTopic);
  } else {
    Serial.print(F("Subscribe failed: "));
    Serial.println(kHeadNodeMqttSubscribeTopic);
  }

  mqttClient.publish(kHeadNodeMqttPublishTopic, "head_node online");
  lastPublishMs = nowMs;
  return true;
}

void publishPeriodicHeartbeat()
{
  if (!mqttClient.connected()) {
    return;
  }

  const uint32_t nowMs = millis();
  if (nowMs - lastPublishMs < kHeadNodeMqttPublishIntervalMs) {
    return;
  }

  char payload[96] = {};
  snprintf(payload, sizeof(payload), "{\"uptime_ms\":%lu,\"mode\":\"HEAD_NODE\"}",
           static_cast<unsigned long>(nowMs));

  if (mqttClient.publish(kHeadNodeMqttPublishTopic, payload)) {
    Serial.print(F("Published: "));
    Serial.println(payload);
  } else {
    logMessage(F("MQTT publish failed"));
  }

  lastPublishMs = nowMs;
}

} // namespace

void initHeadNodeMqtt()
{
  pinMode(kW5500CsPin, OUTPUT);
  digitalWrite(kW5500CsPin, HIGH);

  startEthernet();
  mqttClient.setServer(kHeadNodeMqttBrokerHost, kHeadNodeMqttBrokerPort);
  mqttClient.setCallback(onMqttMessage);
}

void serviceHeadNodeMqtt()
{
  if (ensureMqttConnection()) {
    mqttClient.loop();
    publishPeriodicHeartbeat();
  }
}

bool isHeadNodeMqttConnected()
{
  return mqttClient.connected();
}

bool publishHeadNodeTestMessage(const char *payload)
{
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  if (!ensureMqttConnection()) {
    return false;
  }

  return mqttClient.publish(kHeadNodeMqttPublishTopic, payload);
}