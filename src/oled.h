#ifndef OLED_H
#define OLED_H

#include <Arduino.h>

class OLED
{
public:
    bool begin();

    void clear();

    void setNetworkInfo(bool connected, const String &ip);

    void showReady();

    void showRecording();

    void showPlaying();

    void showNewMessage(const String &senderId);

    void showInbox(size_t index,
                   size_t total,
                   const String &messageId,
                   const String &direction,
                   bool read);

    void showSaved();

    void showText(const String &line1, const String &line2);
};

extern OLED oled;

#endif
