#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

void audioInit();

bool audioStartRecord();
void audioStopRecord();

bool audioStartPlay();
void audioStopPlay();

bool audioRead(int16_t *buffer, size_t samples);
bool audioWrite(const int16_t *buffer, size_t samples);

bool audioIsRecording();
bool audioIsPlaying();

void audioFlush();

#endif
