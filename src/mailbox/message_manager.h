#ifndef MESSAGE_MANAGER_H
#define MESSAGE_MANAGER_H

#include <Arduino.h>
#include <FS.h>

enum MessageDirection
{
    MESSAGE_DIRECTION_SENT,
    MESSAGE_DIRECTION_RECEIVED
};

struct MessageRecord
{
    char id[12];
    uint32_t senderId;
    uint32_t receiverId;
    uint32_t timestamp;
    MessageDirection direction;
    char audioPath[24];
    bool read;
    size_t audioBytes;
};

class MessageManager
{
public:
    bool begin();

    bool startMessage(MessageDirection direction,
                      uint32_t senderId,
                      uint32_t receiverId,
                      MessageRecord &record);
    bool appendAudio(MessageRecord &record, const int16_t *samples, size_t sampleCount);
    void finishAudio();
    bool saveRecord(const MessageRecord &record);
    bool discardAudio(const MessageRecord &record);

    size_t count(MessageDirection direction, bool filterByDirection);
    bool get(size_t index, MessageRecord &record, MessageDirection direction, bool filterByDirection);
    bool markRead(const char *id, bool read);
    bool openAudio(const MessageRecord &record, File &file);

private:
    bool ensureIndexFile();
    uint32_t nextSequence();
    uint32_t currentTimestamp();
    bool parseLine(const String &line, MessageRecord &record);
    String formatRecord(const MessageRecord &record);

    File _activeAudio;
    uint32_t _nextSequence = 1;
    bool _mounted = false;
};

extern MessageManager messageManager;

#endif
