#include <Arduino.h>

#include "config.h"
#include "button.h"
#include "oled.h"
#include "audio.h"
#include "network.h"
#include "mailbox/message_manager.h"

Button ptt;

enum AppState
{
    STATE_READY,
    STATE_TALKING,
    STATE_RECEIVING_MESSAGE,
    STATE_NEW_MESSAGE,
    STATE_INBOX,
    STATE_PLAYING
};

struct ButtonEvent
{
    bool down;
    bool pressed;
    bool released;
    uint32_t heldMs;
    uint32_t releasedMs;
};

static AppState state = STATE_READY;
static int16_t audioBuffer[AUDIO_BLOCK_SIZE];
static uint32_t lastAudioPacketMs = 0;
static uint32_t lastUiUpdateMs = 0;
static uint32_t lastInboxActionMs = 0;

static MessageRecord incomingMessage;
static MessageRecord outgoingMessage;
static bool hasPendingIncoming = false;
static bool receivingIncoming = false;
static bool hasOutgoingMessage = false;
static size_t inboxIndex = 0;
static String pendingSenderLabel;

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

static ButtonEvent readButtonEvent()
{
    static bool lastDown = false;
    static uint32_t pressStartMs = 0;

    ButtonEvent event = {};
    event.down = stableButtonPressed();

    if (event.down && !lastDown)
    {
        pressStartMs = millis();
        event.pressed = true;
    }

    if (event.down)
        event.heldMs = millis() - pressStartMs;

    if (!event.down && lastDown)
    {
        event.released = true;
        event.releasedMs = millis() - pressStartMs;
    }

    lastDown = event.down;
    return event;
}

static String formatDeviceId(uint32_t deviceId)
{
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%08lX", (unsigned long)deviceId);
    return String(buffer);
}

static const char *directionText(MessageDirection direction)
{
    return direction == MESSAGE_DIRECTION_SENT ? "sent" : "received";
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

static void showSavedMoment()
{
    updateNetworkUi();
    oled.showSaved();
    delay(500);
}

static void enterReady()
{
    audioStopRecord();
    audioStopPlay();
    state = STATE_READY;
    showReady();
    Serial.println("State: READY");
}

static bool savePendingIncoming(bool read, bool showSaved)
{
    if (!hasPendingIncoming)
        return false;

    messageManager.finishAudio();
    receivingIncoming = false;
    incomingMessage.read = read;

    bool saved = false;
    if (incomingMessage.audioBytes > 0)
    {
        saved = messageManager.saveRecord(incomingMessage);
        Serial.print("Incoming message saved: ");
        Serial.println(incomingMessage.id);
    }
    else
    {
        messageManager.discardAudio(incomingMessage);
    }

    hasPendingIncoming = false;

    if (showSaved && saved)
        showSavedMoment();

    return saved;
}

static bool playMessageAudio(MessageRecord &record)
{
    messageManager.finishAudio();
    audioStopRecord();

    File file;
    if (!messageManager.openAudio(record, file))
    {
        Serial.print("Open audio failed: ");
        Serial.println(record.audioPath);
        return false;
    }

    if (!audioStartPlay())
    {
        Serial.println("Audio TX start failed");
        file.close();
        return false;
    }

    state = STATE_PLAYING;
    oled.showPlaying();
    Serial.print("Playing message: ");
    Serial.println(record.id);

    while (file.available())
    {
        size_t bytesRead = file.read((uint8_t *)audioBuffer, sizeof(audioBuffer));
        if (bytesRead == 0)
            break;

        size_t samples = bytesRead / sizeof(int16_t);
        if (samples > 0)
            audioWrite(audioBuffer, samples);

        networkLoop();
        delay(1);
    }

    file.close();
    audioStopPlay();

    if (record.direction == MESSAGE_DIRECTION_RECEIVED)
    {
        record.read = true;
        messageManager.markRead(record.id, true);
    }

    return true;
}

static void listenPendingIncoming()
{
    if (!hasPendingIncoming)
    {
        enterReady();
        return;
    }

    messageManager.finishAudio();
    receivingIncoming = false;

    playMessageAudio(incomingMessage);

    incomingMessage.read = true;
    if (incomingMessage.audioBytes > 0)
        messageManager.saveRecord(incomingMessage);
    else
        messageManager.discardAudio(incomingMessage);

    hasPendingIncoming = false;
    enterReady();
}

static bool showInboxEntry()
{
    size_t total = messageManager.count(MESSAGE_DIRECTION_SENT, false);
    if (total == 0)
    {
        oled.showText("INBOX", "Empty");
        return false;
    }

    if (inboxIndex >= total)
        inboxIndex = 0;

    MessageRecord record;
    if (!messageManager.get(inboxIndex, record, MESSAGE_DIRECTION_SENT, false))
    {
        oled.showText("INBOX", "Read error");
        return false;
    }

    oled.showInbox(inboxIndex,
                   total,
                   String(record.id),
                   String(directionText(record.direction)),
                   record.read);
    return true;
}

static void enterInbox()
{
    audioStopRecord();
    audioStopPlay();

    state = STATE_INBOX;
    lastInboxActionMs = millis();

    if (!showInboxEntry())
    {
        delay(700);
        enterReady();
    }
    else
    {
        Serial.println("State: INBOX");
    }
}

static void handleInbox(const ButtonEvent &button)
{
    if (millis() - lastInboxActionMs > INBOX_IDLE_TIMEOUT_MS)
    {
        enterReady();
        return;
    }

    if (!button.released)
        return;

    lastInboxActionMs = millis();

    size_t total = messageManager.count(MESSAGE_DIRECTION_SENT, false);
    if (total == 0)
    {
        enterReady();
        return;
    }

    if (button.releasedMs >= BUTTON_LONG_PRESS_MS)
    {
        MessageRecord record;
        if (messageManager.get(inboxIndex, record, MESSAGE_DIRECTION_SENT, false))
        {
            playMessageAudio(record);
            state = STATE_INBOX;
            showInboxEntry();
        }
        return;
    }

    inboxIndex = (inboxIndex + 1) % total;
    showInboxEntry();
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

    hasOutgoingMessage = messageManager.startMessage(
        MESSAGE_DIRECTION_SENT,
        networkLocalDeviceId(),
        0,
        outgoingMessage);

    if (!hasOutgoingMessage)
        Serial.println("Outgoing history start failed");

    state = STATE_TALKING;
    oled.showRecording();
    Serial.println("State: TALKING");
}

static void finishTalking(uint32_t pressDurationMs)
{
    audioStopRecord();

    bool saved = false;
    bool openInbox = pressDurationMs <= BUTTON_TAP_MAX_MS;

    if (hasOutgoingMessage)
    {
        messageManager.finishAudio();

        if (outgoingMessage.audioBytes > 0)
        {
            saved = messageManager.saveRecord(outgoingMessage);
            openInbox = false;
            Serial.print("Outgoing message saved: ");
            Serial.println(outgoingMessage.id);
        }
        else
        {
            messageManager.discardAudio(outgoingMessage);
        }
    }

    hasOutgoingMessage = false;

    if (saved)
        showSavedMoment();

    if (openInbox)
        enterInbox();
    else
        enterReady();
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
        return;
    }

    if (hasOutgoingMessage && !messageManager.appendAudio(outgoingMessage, audioBuffer, AUDIO_BLOCK_SIZE))
        Serial.println("Outgoing history write failed");
}

static bool startIncomingMessage(uint32_t senderDeviceId)
{
    hasPendingIncoming = messageManager.startMessage(
        MESSAGE_DIRECTION_RECEIVED,
        senderDeviceId,
        networkLocalDeviceId(),
        incomingMessage);

    if (!hasPendingIncoming)
    {
        Serial.println("Incoming history start failed");
        return false;
    }

    pendingSenderLabel = formatDeviceId(senderDeviceId);
    receivingIncoming = true;
    lastAudioPacketMs = millis();
    state = STATE_RECEIVING_MESSAGE;
    oled.showNewMessage(pendingSenderLabel);

    Serial.print("State: NEW MESSAGE from ");
    Serial.println(pendingSenderLabel);
    return true;
}

static void handleIncoming()
{
    if (state == STATE_TALKING || state == STATE_PLAYING)
        return;

    uint32_t senderDeviceId = 0;
    int samples = networkReceiveAudioFrom(audioBuffer, AUDIO_BLOCK_SIZE, &senderDeviceId);

    if (samples > 0)
    {
        if (hasPendingIncoming && !receivingIncoming)
            savePendingIncoming(false, false);

        if (!hasPendingIncoming && !startIncomingMessage(senderDeviceId))
            return;

        if (!messageManager.appendAudio(incomingMessage, audioBuffer, (size_t)samples))
            Serial.println("Incoming history write failed");

        lastAudioPacketMs = millis();
        oled.showNewMessage(pendingSenderLabel);
        return;
    }

    if (receivingIncoming && millis() - lastAudioPacketMs > MESSAGE_END_TIMEOUT_MS)
    {
        messageManager.finishAudio();
        receivingIncoming = false;

        if (incomingMessage.audioBytes == 0)
        {
            messageManager.discardAudio(incomingMessage);
            hasPendingIncoming = false;
            enterReady();
            return;
        }

        state = STATE_NEW_MESSAGE;
        oled.showNewMessage(pendingSenderLabel);
        Serial.println("Incoming message ready for user action");
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

    if (!messageManager.begin())
        Serial.println("Mailbox storage init failed");
    else
        Serial.println("Mailbox storage ready");

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

    ButtonEvent button = readButtonEvent();

    if (state == STATE_READY && button.pressed)
        enterTalking();

    if (state == STATE_TALKING)
    {
        if (button.down)
        {
            handleTalking();
            delay(1);
            return;
        }

        finishTalking(button.released ? button.releasedMs : 0);
        delay(1);
        return;
    }

    handleIncoming();

    if (state == STATE_NEW_MESSAGE && !receivingIncoming && button.released)
    {
        if (button.releasedMs >= BUTTON_LONG_PRESS_MS)
        {
            savePendingIncoming(false, true);
            enterReady();
        }
        else
        {
            listenPendingIncoming();
        }
    }
    else if (state == STATE_INBOX)
    {
        handleInbox(button);
    }

    if (millis() - lastUiUpdateMs > 1000)
    {
        lastUiUpdateMs = millis();
        updateNetworkUi();

        if (state == STATE_READY)
            oled.showReady();
    }

    delay(1);
}
