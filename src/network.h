#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

bool networkBegin();
void networkLoop();

bool networkIsConnected();
String networkLocalIP();

bool networkSendAudio(const int16_t *samples, size_t sampleCount);
int networkReceiveAudio(int16_t *samples, size_t maxSamples);

#endif
