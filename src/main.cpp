#include <Arduino.h>

#include "config.h"
#include "button.h"
#include "oled.h"
#include "audio.h"
#include "network.h"

Button ptt;

enum AppState
{
    STATE_READY,
    STATE_TALKING,
    STATE_RECEIVING
};

static AppState state = STATE_READY;
static int16_t audioBuffer[AUDIO_BLOCK_SIZE];
static uint32_t lastAudioPacketMs = 0;
static uint32_t lastUiUpdateMs = 0;

static bool stableButtonPressed()
{
    static bool stable = false;
    static bool lastRaw = false;
    static uint32_t lastChangeMs = 0;

    bool raw = ptt.pressed();

    if (raw != lastRaw)
    {
        lastRaw = raw;
        lastChangeMs = millis();
    }

    if (millis() - lastChangeMs >= 25)
    {
        stable = raw;
    }

    return stable;
}

static void amplifyBuffer(int16_t *buffer, size_t samples)
{
    for (size_t i = 0; i < samples; i++)
    {
        int32_t sample = buffer[i];

        if (sample > -AUDIO_NOISE_GATE && sample < AUDIO_NOISE_GATE)
        {
            buffer[i] = 0;
            continue;
        }

        sample *= STREAM_GAIN;

        if (sample > 32767)
            sample = 32767;
        else if (sample < -32768)
            sample = -32768;

        buffer[i] = (int16_t)sample;
    }
}

static int16_t calcPeak(const int16_t *buffer, size_t samples)
{
    int16_t peak = 0;

    for (size_t i = 0; i < samples; i++)
    {
        int32_t sample = buffer[i];
        if (sample < 0)
            sample = -sample;

        if (sample > peak)
            peak = (int16_t)sample;
    }

    return peak;
}

static void updateNetworkUi()
{
    oled.setNetworkInfo(networkIsConnected(), networkLocalIP());
}

static void showReady()
{
    updateNetworkUi();
    oled.showReady();
}

static void enterReady()
{
    audioStopRecord();
    audioStopPlay();
    state = STATE_READY;
    showReady();
    Serial.println("State: READY");
}

static void enterTalking()
{
    audioStopPlay();

    if (!audioStartRecord())
    {
        Serial.println("Audio RX start failed");
        enterReady();
        return;
    }

    audioFlush();
    state = STATE_TALKING;
    oled.showRecording();
    Serial.println("State: TALKING");
}

static void enterReceiving()
{
    audioStopRecord();

    if (!audioStartPlay())
    {
        Serial.println("Audio TX start failed");
        enterReady();
        return;
    }

    state = STATE_RECEIVING;
    oled.showPlaying();
    Serial.println("State: RECEIVING");
}

static void handleTalking()
{
    if (!audioRead(audioBuffer, AUDIO_BLOCK_SIZE))
    {
        Serial.println("Audio read failed");
        return;
    }

    amplifyBuffer(audioBuffer, AUDIO_BLOCK_SIZE);

    int16_t peak = calcPeak(audioBuffer, AUDIO_BLOCK_SIZE);
    if (peak < VOICE_SEND_THRESHOLD)
    {
        return;
    }

    if (!networkSendAudio(audioBuffer, AUDIO_BLOCK_SIZE))
    {
        Serial.println("UDP send failed");
    }
}

static void handleReceiving()
{
    int samples = networkReceiveAudio(audioBuffer, AUDIO_BLOCK_SIZE);

    if (samples > 0)
    {
        if (state != STATE_RECEIVING)
            enterReceiving();

        audioWrite(audioBuffer, (size_t)samples);
        lastAudioPacketMs = millis();
        return;
    }

    if (state == STATE_RECEIVING && millis() - lastAudioPacketMs > 300)
    {
        enterReady();
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("ESP32 Intercom V2 UDP");

    ptt.begin(BUTTON_PIN);
    oled.begin();
    audioInit();

    oled.setNetworkInfo(false, "0.0.0.0");
    oled.showText("ESP TALK", "WiFi...");

    bool connected = networkBegin();
    oled.setNetworkInfo(connected, networkLocalIP());
    showReady();

    Serial.println("Init done");
}

void loop()
{
    networkLoop();

    if (millis() - lastUiUpdateMs > 1000)
    {
        lastUiUpdateMs = millis();
        updateNetworkUi();

        if (state == STATE_READY)
            oled.showReady();
    }

    bool pressed = stableButtonPressed();

    if (pressed)
    {
        if (state != STATE_TALKING)
            enterTalking();

        handleTalking();
        return;
    }

    if (state == STATE_TALKING)
        enterReady();

    handleReceiving();

    delay(1);
}
