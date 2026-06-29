#include "message_manager.h"

#include <SPIFFS.h>
#include <time.h>

static const char *INDEX_PATH = "/msgs.csv";

MessageManager messageManager;

static const char *directionToText(MessageDirection direction)
{
    return direction == MESSAGE_DIRECTION_SENT ? "sent" : "received";
}

static MessageDirection textToDirection(const String &value)
{
    return value == "sent" ? MESSAGE_DIRECTION_SENT : MESSAGE_DIRECTION_RECEIVED;
}

bool MessageManager::begin()
{
    _mounted = SPIFFS.begin(true);
    if (!_mounted)
        return false;

    if (!ensureIndexFile())
        return false;

    File indexFile = SPIFFS.open(INDEX_PATH, FILE_READ);
    if (!indexFile)
        return false;

    uint32_t maxSequence = 0;
    while (indexFile.available())
    {
        String line = indexFile.readStringUntil('\n');
        line.trim();
        if (line.length() < 2)
            continue;

        uint32_t sequence = (uint32_t)line.substring(1, 7).toInt();
        if (sequence > maxSequence)
            maxSequence = sequence;
    }

    _nextSequence = maxSequence + 1;
    if (_nextSequence == 0)
        _nextSequence = 1;

    return true;
}

bool MessageManager::ensureIndexFile()
{
    if (SPIFFS.exists(INDEX_PATH))
        return true;

    File indexFile = SPIFFS.open(INDEX_PATH, FILE_WRITE);
    if (!indexFile)
        return false;

    indexFile.close();
    return true;
}

uint32_t MessageManager::nextSequence()
{
    uint32_t sequence = _nextSequence++;
    if (_nextSequence > 999999)
        _nextSequence = 1;

    return sequence;
}

uint32_t MessageManager::currentTimestamp()
{
    time_t now = time(nullptr);
    if (now > 100000)
        return (uint32_t)now;

    return millis() / 1000;
}

bool MessageManager::startMessage(MessageDirection direction,
                                  uint32_t senderId,
                                  uint32_t receiverId,
                                  MessageRecord &record)
{
    if (!_mounted)
        return false;

    finishAudio();

    memset(&record, 0, sizeof(record));

    uint32_t sequence = nextSequence();
    char prefix = direction == MESSAGE_DIRECTION_SENT ? 's' : 'r';

    snprintf(record.id, sizeof(record.id), "%c%06lu", prefix, (unsigned long)sequence);
    snprintf(record.audioPath, sizeof(record.audioPath), "/%s.raw", record.id);

    record.senderId = senderId;
    record.receiverId = receiverId;
    record.timestamp = currentTimestamp();
    record.direction = direction;
    record.read = direction == MESSAGE_DIRECTION_SENT;
    record.audioBytes = 0;

    _activeAudio = SPIFFS.open(record.audioPath, FILE_WRITE);
    return (bool)_activeAudio;
}

bool MessageManager::appendAudio(MessageRecord &record, const int16_t *samples, size_t sampleCount)
{
    if (!_activeAudio || samples == nullptr || sampleCount == 0)
        return false;

    size_t bytes = sampleCount * sizeof(int16_t);
    size_t written = _activeAudio.write((const uint8_t *)samples, bytes);
    record.audioBytes += written;
    return written == bytes;
}

void MessageManager::finishAudio()
{
    if (_activeAudio)
        _activeAudio.close();
}

bool MessageManager::saveRecord(const MessageRecord &record)
{
    if (!_mounted || record.id[0] == '\0' || record.audioPath[0] == '\0')
        return false;

    File indexFile = SPIFFS.open(INDEX_PATH, FILE_APPEND);
    if (!indexFile)
        return false;

    String line = formatRecord(record);
    size_t written = indexFile.print(line);
    indexFile.close();

    return written == line.length();
}

bool MessageManager::discardAudio(const MessageRecord &record)
{
    finishAudio();

    if (!_mounted || record.audioPath[0] == '\0')
        return false;

    if (!SPIFFS.exists(record.audioPath))
        return true;

    return SPIFFS.remove(record.audioPath);
}

size_t MessageManager::count(MessageDirection direction, bool filterByDirection)
{
    if (!_mounted)
        return 0;

    File indexFile = SPIFFS.open(INDEX_PATH, FILE_READ);
    if (!indexFile)
        return 0;

    size_t total = 0;
    while (indexFile.available())
    {
        String line = indexFile.readStringUntil('\n');
        MessageRecord record;
        if (!parseLine(line, record))
            continue;

        if (!filterByDirection || record.direction == direction)
            total++;
    }

    return total;
}

bool MessageManager::get(size_t indexToFind,
                         MessageRecord &record,
                         MessageDirection direction,
                         bool filterByDirection)
{
    if (!_mounted)
        return false;

    File indexFile = SPIFFS.open(INDEX_PATH, FILE_READ);
    if (!indexFile)
        return false;

    size_t matched = 0;
    while (indexFile.available())
    {
        String line = indexFile.readStringUntil('\n');
        MessageRecord parsed;
        if (!parseLine(line, parsed))
            continue;

        if (filterByDirection && parsed.direction != direction)
            continue;

        if (matched == indexToFind)
        {
            record = parsed;
            return true;
        }

        matched++;
    }

    return false;
}

bool MessageManager::markRead(const char *id, bool read)
{
    if (!_mounted || id == nullptr || id[0] == '\0')
        return false;

    File indexFile = SPIFFS.open(INDEX_PATH, FILE_READ);
    if (!indexFile)
        return false;

    String rewritten;
    bool updated = false;
    while (indexFile.available())
    {
        String line = indexFile.readStringUntil('\n');
        MessageRecord record;

        if (!parseLine(line, record))
            continue;

        if (strcmp(record.id, id) == 0)
        {
            record.read = read;
            updated = true;
        }

        rewritten += formatRecord(record);
    }
    indexFile.close();

    if (!updated)
        return false;

    File out = SPIFFS.open(INDEX_PATH, FILE_WRITE);
    if (!out)
        return false;

    size_t written = out.print(rewritten);
    out.close();

    return written == rewritten.length();
}

bool MessageManager::openAudio(const MessageRecord &record, File &file)
{
    if (!_mounted || record.audioPath[0] == '\0')
        return false;

    file = SPIFFS.open(record.audioPath, FILE_READ);
    return (bool)file;
}

bool MessageManager::parseLine(const String &line, MessageRecord &record)
{
    String fields[8];
    int start = 0;

    for (int i = 0; i < 8; i++)
    {
        int end = line.indexOf('|', start);
        if (end < 0)
        {
            if (i != 7)
                return false;

            fields[i] = line.substring(start);
        }
        else
        {
            fields[i] = line.substring(start, end);
            start = end + 1;
        }
    }

    fields[7].trim();
    if (fields[0].length() == 0 || fields[5].length() == 0)
        return false;

    memset(&record, 0, sizeof(record));

    strncpy(record.id, fields[0].c_str(), sizeof(record.id) - 1);
    record.senderId = (uint32_t)strtoul(fields[1].c_str(), nullptr, 10);
    record.receiverId = (uint32_t)strtoul(fields[2].c_str(), nullptr, 10);
    record.timestamp = (uint32_t)strtoul(fields[3].c_str(), nullptr, 10);
    record.direction = textToDirection(fields[4]);
    strncpy(record.audioPath, fields[5].c_str(), sizeof(record.audioPath) - 1);
    record.read = fields[6] == "1";
    record.audioBytes = (size_t)strtoul(fields[7].c_str(), nullptr, 10);

    return true;
}

String MessageManager::formatRecord(const MessageRecord &record)
{
    String line;
    line.reserve(96);
    line += record.id;
    line += "|";
    line += String(record.senderId);
    line += "|";
    line += String(record.receiverId);
    line += "|";
    line += String(record.timestamp);
    line += "|";
    line += directionToText(record.direction);
    line += "|";
    line += record.audioPath;
    line += "|";
    line += record.read ? "1" : "0";
    line += "|";
    line += String((unsigned long)record.audioBytes);
    line += "\n";
    return line;
}
