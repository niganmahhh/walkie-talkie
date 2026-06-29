#include "network.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiUdp.h>

static WiFiUDP udp;
static IPAddress broadcastIp(255, 255, 255, 255);
static IPAddress relayIp;
static uint32_t localDeviceId = 0;
static uint32_t sequenceNumber = 0;
static uint32_t lastReconnectAttemptMs = 0;
static uint32_t lastKeepaliveMs = 0;
static bool udpReady = false;
static bool relayReady = false;

struct __attribute__((packed)) AudioPacketHeader
{
    uint32_t magic;
    uint32_t deviceId;
    uint32_t sequence;
    uint16_t sampleCount;
};

static constexpr uint32_t PACKET_MAGIC = 0x31544B45; // "EKT1" little-endian
static uint8_t rxPacket[sizeof(AudioPacketHeader) + AUDIO_BLOCK_SIZE * sizeof(int16_t)];

static uint32_t makeDeviceId()
{
    uint8_t mac[6];
    WiFi.macAddress(mac);

    uint32_t id = 0;
    id |= (uint32_t)mac[2] << 24;
    id |= (uint32_t)mac[3] << 16;
    id |= (uint32_t)mac[4] << 8;
    id |= (uint32_t)mac[5];
    return id;
}

static void startWifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

static bool resolveRelay()
{
#if NETWORK_MODE == NETWORK_MODE_RELAY
    if (relayReady)
        return true;

    if (WiFi.hostByName(RELAY_HOST, relayIp) != 1)
    {
        Serial.print("Relay DNS failed: ");
        Serial.println(RELAY_HOST);
        return false;
    }

    relayReady = true;

    Serial.print("Relay resolved: ");
    Serial.print(RELAY_HOST);
    Serial.print(" -> ");
    Serial.println(relayIp);
    return true;
#else
    return true;
#endif
}

static bool sendPacket(const int16_t *samples, size_t sampleCount)
{
    if (!networkIsConnected() || !udpReady)
        return false;

    if (sampleCount > AUDIO_BLOCK_SIZE)
        sampleCount = AUDIO_BLOCK_SIZE;

    AudioPacketHeader header;
    header.magic = PACKET_MAGIC;
    header.deviceId = localDeviceId;
    header.sequence = sequenceNumber++;
    header.sampleCount = (uint16_t)sampleCount;

#if NETWORK_MODE == NETWORK_MODE_RELAY
    if (!resolveRelay())
        return false;

    if (!udp.beginPacket(relayIp, RELAY_PORT))
        return false;
#else
    if (!udp.beginPacket(broadcastIp, AUDIO_UDP_PORT))
        return false;
#endif

    udp.write((const uint8_t *)&header, sizeof(header));

    if (samples != nullptr && sampleCount > 0)
        udp.write((const uint8_t *)samples, sampleCount * sizeof(int16_t));

    return udp.endPacket() == 1;
}

static void sendKeepalive()
{
#if NETWORK_MODE == NETWORK_MODE_RELAY
    if (millis() - lastKeepaliveMs < RELAY_KEEPALIVE_MS)
        return;

    lastKeepaliveMs = millis();
    sendPacket(nullptr, 0);
#endif
}

bool networkBegin()
{
    localDeviceId = makeDeviceId();

    Serial.print("Device ID: ");
    Serial.println(localDeviceId, HEX);

    startWifi();

    Serial.print("WiFi connecting");
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000)
    {
        Serial.print(".");
        delay(300);
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi connect failed");
        return false;
    }

    if (!udp.begin(AUDIO_UDP_PORT))
    {
        Serial.println("UDP begin failed");
        return false;
    }

    udpReady = true;
    relayReady = false;
    resolveRelay();

    Serial.print("WiFi connected, IP=");
    Serial.println(WiFi.localIP());
    Serial.print("UDP port=");
    Serial.println(AUDIO_UDP_PORT);
#if NETWORK_MODE == NETWORK_MODE_RELAY
    Serial.println("Network mode: RELAY");
#else
    Serial.println("Network mode: LAN");
#endif
    return true;
}

void networkLoop()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        if (!udpReady)
        {
            udpReady = udp.begin(AUDIO_UDP_PORT);
            if (udpReady)
            {
                Serial.print("UDP ready, IP=");
                Serial.println(WiFi.localIP());
                relayReady = false;
                resolveRelay();
            }
        }

        sendKeepalive();
        return;
    }

    udpReady = false;
    relayReady = false;

    if (millis() - lastReconnectAttemptMs < 5000)
        return;

    lastReconnectAttemptMs = millis();
    Serial.println("WiFi reconnecting...");
    startWifi();
}

bool networkIsConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

String networkLocalIP()
{
    if (WiFi.status() != WL_CONNECTED)
        return "0.0.0.0";

    return WiFi.localIP().toString();
}

uint32_t networkLocalDeviceId()
{
    return localDeviceId;
}

bool networkSendAudio(const int16_t *samples, size_t sampleCount)
{
    if (samples == nullptr || sampleCount == 0)
        return false;

    return sendPacket(samples, sampleCount);
}

int networkReceiveAudioFrom(int16_t *samples, size_t maxSamples, uint32_t *senderDeviceId)
{
    if (!networkIsConnected() || samples == nullptr || maxSamples == 0)
        return 0;

    int packetSize = udp.parsePacket();
    if (packetSize <= 0)
        return 0;

    if ((size_t)packetSize > sizeof(rxPacket))
    {
        while (udp.available())
            udp.read();
        return 0;
    }

    int bytesRead = udp.read(rxPacket, sizeof(rxPacket));
    if (bytesRead < (int)sizeof(AudioPacketHeader))
        return 0;

    AudioPacketHeader header;
    memcpy(&header, rxPacket, sizeof(header));

    if (header.magic != PACKET_MAGIC)
        return 0;

    if (header.deviceId == localDeviceId)
        return 0;

    if (header.sampleCount == 0 || header.sampleCount > AUDIO_BLOCK_SIZE)
        return 0;

    size_t payloadBytes = (size_t)header.sampleCount * sizeof(int16_t);
    if ((size_t)bytesRead < sizeof(AudioPacketHeader) + payloadBytes)
        return 0;

    size_t copySamples = header.sampleCount;
    if (copySamples > maxSamples)
        copySamples = maxSamples;

    memcpy(samples, rxPacket + sizeof(AudioPacketHeader), copySamples * sizeof(int16_t));

    if (senderDeviceId != nullptr)
        *senderDeviceId = header.deviceId;

    return (int)copySamples;
}

int networkReceiveAudio(int16_t *samples, size_t maxSamples)
{
    return networkReceiveAudioFrom(samples, maxSamples, nullptr);
}
