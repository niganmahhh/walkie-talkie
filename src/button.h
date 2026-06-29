#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

class Button
{
public:
    void begin(uint8_t pin)
    {
        _pin = pin;
        pinMode(_pin, INPUT_PULLUP);
    }

    bool pressed()
    {
        return digitalRead(_pin) == LOW;
    }

    bool released()
    {
        return !pressed();
    }

private:
    uint8_t _pin;
};

#endif
