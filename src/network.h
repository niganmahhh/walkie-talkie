#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

bool networkBegin();
void networkLoop();

bool networkIsConnected();
String networkLocalIP();
uint32_t networkLocalDeviceId();

bool networkSendAudio(const int16_t *samples, size_t sampleCount);
int networkReceiveAudio(int16_t *samples, size_t maxSamples);
int networkReceiveAudioFrom(int16_t *samples, size_t maxSamples, uint32_t *senderDeviceId);

#endif
