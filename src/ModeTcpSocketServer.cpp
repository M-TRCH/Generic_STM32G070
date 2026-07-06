#include "ModeTcpSocketServer.h"

#include <Arduino.h>
#include <Ethernet.h>
#include <SPI.h>
#include <string.h>

#include "Config.h"
#include "Hardware.h"

namespace {

constexpr float kVoltageMin = 24.0f;
constexpr float kVoltageMax = 29.2f;
constexpr float kCurrentMin = -12.0f;
constexpr float kCurrentMax = 12.0f;
constexpr float kBatteryPercentMin = 5.0f;
constexpr float kBatteryPercentMax = 100.0f;
constexpr float kTemperatureMin = 20.0f;
constexpr float kTemperatureMax = 38.0f;
constexpr float kHumidityMin = 35.0f;
constexpr float kHumidityMax = 85.0f;
constexpr float kRemainingUsageMin = 0.25f;
constexpr float kRemainingUsageMax = 18.0f;
constexpr float kFullChargeMin = 0.25f;
constexpr float kFullChargeMax = 12.0f;

EthernetServer server(kTcpSocketServerPort);
EthernetClient activeClient;

bool ethernetReady = false;

IPAddress makeIpAddress(const uint8_t raw[4])
{
  return IPAddress(raw[0], raw[1], raw[2], raw[3]);
}

void printServerBanner()
{
  Serial.println(F("--- TCP Socket Server Test Mode ---"));
  Serial.print(F("MAC: "));
  for (size_t index = 0; index < sizeof(kHeadNodeMacAddress); ++index) {
    if (index > 0u) {
      Serial.print(F(":"));
    }
    if (kHeadNodeMacAddress[index] < 0x10u) {
      Serial.print(F("0"));
    }
    Serial.print(kHeadNodeMacAddress[index], HEX);
  }
  Serial.println();
  Serial.print(F("TCP port: "));
  Serial.println(kTcpSocketServerPort);
  Serial.print(F("Packet size: "));
  Serial.print(sizeof(BatteryTelemetryTcpPacket));
  Serial.println(F(" bytes"));
  Serial.print(F("IP mode: "));
  Serial.println(kTcpSocketUseStaticIp ? F("static") : F("dhcp"));
  if (kTcpSocketUseStaticIp) {
    Serial.print(F("Static IP: "));
    Serial.println(makeIpAddress(kTcpSocketStaticIp));
    Serial.print(F("Gateway: "));
    Serial.println(makeIpAddress(kTcpSocketGateway));
    Serial.print(F("Subnet: "));
    Serial.println(makeIpAddress(kTcpSocketSubnetMask));
    Serial.print(F("DNS: "));
    Serial.println(makeIpAddress(kTcpSocketDnsServer));
  }
}

void printPacketSummary(const BatteryTelemetryTcpPacket &packet)
{
  Serial.print(F("TCP TX state="));
  Serial.print(packet.batState);
  Serial.print(F(" V="));
  Serial.print(packet.voltageV, 2);
  Serial.print(F(" I="));
  Serial.print(packet.currentA, 2);
  Serial.print(F(" SoC="));
  Serial.print(packet.batteryPercent, 1);
  Serial.print(F(" T="));
  Serial.print(packet.temperatureC, 1);
  Serial.print(F(" H="));
  Serial.print(packet.humidityPercent, 1);
  Serial.print(F(" RemH="));
  Serial.print(packet.remainingUsageTimeHours, 2);
  Serial.print(F(" FullH="));
  Serial.println(packet.fullChargeEstimateTimeHours, 2);
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

  if (kTcpSocketUseStaticIp) {
    const IPAddress localIp = makeIpAddress(kTcpSocketStaticIp);
    const IPAddress dnsServer = makeIpAddress(kTcpSocketDnsServer);
    const IPAddress gateway = makeIpAddress(kTcpSocketGateway);
    const IPAddress subnetMask = makeIpAddress(kTcpSocketSubnetMask);

    Serial.println(F("Starting Ethernet (Static IP)..."));
    Ethernet.begin(const_cast<uint8_t *>(kHeadNodeMacAddress), localIp, dnsServer, gateway, subnetMask);
  } else {
    Serial.println(F("Starting Ethernet (DHCP)..."));
    if (Ethernet.begin(const_cast<uint8_t *>(kHeadNodeMacAddress)) == 0) {
      ethernetReady = false;
      Serial.println(F("Ethernet DHCP failed"));
      return;
    }
  }

  server.begin();
  ethernetReady = true;
  Serial.print(F("TCP server listening on "));
  Serial.print(Ethernet.localIP());
  Serial.print(F(":"));
  Serial.println(kTcpSocketServerPort);
}

float randomFloat(float minValue, float maxValue)
{
  const long scaledValue = random(0L, 10001L);
  const float ratio = static_cast<float>(scaledValue) / 10000.0f;
  return minValue + ((maxValue - minValue) * ratio);
}

void copyBatState(BatteryTelemetryTcpPacket &packet, const char *stateText)
{
  memset(packet.batState, 0, sizeof(packet.batState));
  if (stateText != nullptr) {
    strncpy(packet.batState, stateText, sizeof(packet.batState) - 1u);
  }
}

const char *randomBatteryState()
{
  switch (random(0L, 3L)) {
    case 0:
      return "charge";
    case 1:
      return "discharge";
    default:
      return "full";
  }
}

void populateRandomPacket(BatteryTelemetryTcpPacket &packet)
{
  const char *stateText = randomBatteryState();

  packet.voltageV = randomFloat(kVoltageMin, kVoltageMax);
  packet.batteryPercent = randomFloat(kBatteryPercentMin, kBatteryPercentMax);
  packet.temperatureC = randomFloat(kTemperatureMin, kTemperatureMax);
  packet.humidityPercent = randomFloat(kHumidityMin, kHumidityMax);

  if (strcmp(stateText, "full") == 0) {
    packet.currentA = randomFloat(-0.15f, 0.15f);
    packet.batteryPercent = randomFloat(99.0f, 100.0f);
    packet.remainingUsageTimeHours = randomFloat(kRemainingUsageMin, kRemainingUsageMax);
    packet.fullChargeEstimateTimeHours = 0.0f;
  } else if (strcmp(stateText, "charge") == 0) {
    packet.currentA = randomFloat(0.2f, kCurrentMax);
    packet.remainingUsageTimeHours = randomFloat(kRemainingUsageMin, kRemainingUsageMax * 0.5f);
    packet.fullChargeEstimateTimeHours = randomFloat(kFullChargeMin, kFullChargeMax);
  } else {
    packet.currentA = randomFloat(kCurrentMin, -0.2f);
    packet.remainingUsageTimeHours = randomFloat(kRemainingUsageMin, kRemainingUsageMax);
    packet.fullChargeEstimateTimeHours = randomFloat(kFullChargeMin, kFullChargeMax * 0.25f);
  }

  copyBatState(packet, stateText);
}

void serviceTcpClient()
{
  if (!ethernetReady) {
    return;
  }

  Ethernet.maintain();

  if (activeClient && !activeClient.connected()) {
    activeClient.stop();
    Serial.println(F("TCP client disconnected"));
  }

  if (!activeClient) {
    EthernetClient incomingClient = server.accept();
    if (incomingClient) {
      activeClient = incomingClient;
      Serial.print(F("TCP client connected: "));
      Serial.println(activeClient.remoteIP());
    }
  }
}

void sendPacket(const BatteryTelemetryTcpPacket &packet)
{
  if (!activeClient || !activeClient.connected()) {
    return;
  }

  const uint8_t *payload = reinterpret_cast<const uint8_t *>(&packet);
  const size_t packetSize = sizeof(packet);
  size_t written = activeClient.write(payload, packetSize);
  if (written != packetSize) {
    Serial.println(F("TCP write incomplete"));
    activeClient.stop();
  }
}

} // namespace

void tcpSocketServerSetup()
{
  initBoard();
  randomSeed(static_cast<unsigned long>(micros()));
  printServerBanner();

  pinMode(kW5500CsPin, OUTPUT);
  digitalWrite(kW5500CsPin, HIGH);
  startEthernet();
}

void tcpSocketServerLoop()
{
  static uint32_t lastSendMs = 0;
  static BatteryTelemetryTcpPacket packet;

  serviceTcpClient();

  const uint32_t nowMs = millis();

  if (nowMs - lastSendMs < kTcpSocketSendIntervalMs) {
    return;
  }

  lastSendMs = nowMs;
  populateRandomPacket(packet);
  printPacketSummary(packet);
  sendPacket(packet);
}