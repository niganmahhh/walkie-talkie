#include "audio.h"
#include "config.h"

#include <Arduino.h>
#include <driver/i2s.h>

#define I2S_PORT I2S_NUM_0

static bool recording = false;
static bool playing = false;
static bool driverInstalled = false;

static void uninstallDriver()
{
    if (driverInstalled)
    {
        i2s_stop(I2S_PORT);
        i2s_driver_uninstall(I2S_PORT);
        driverInstalled = false;
    }
}

static bool installRx()
{
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin = {
        .bck_io_num = MIC_BCLK,
        .ws_io_num = MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_SD
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK)
        return false;

    driverInstalled = true;

    err = i2s_set_pin(I2S_PORT, &pin);
    if (err != ESP_OK)
    {
        uninstallDriver();
        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    i2s_start(I2S_PORT);
    return true;
}

static bool installTx()
{
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin = {
        .bck_io_num = SPK_BCLK,
        .ws_io_num = SPK_WS,
        .data_out_num = SPK_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK)
        return false;

    driverInstalled = true;

    err = i2s_set_pin(I2S_PORT, &pin);
    if (err != ESP_OK)
    {
        uninstallDriver();
        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    i2s_start(I2S_PORT);
    return true;
}

void audioInit()
{
    recording = false;
    playing = false;
    uninstallDriver();
}

bool audioStartRecord()
{
    if (recording)
        return true;

    playing = false;
    uninstallDriver();

    if (!installRx())
        return false;

    recording = true;
    return true;
}

void audioStopRecord()
{
    if (!recording)
        return;

    recording = false;
    uninstallDriver();
}

bool audioStartPlay()
{
    if (playing)
        return true;

    recording = false;
    uninstallDriver();

    if (!installTx())
        return false;

    playing = true;
    return true;
}

void audioStopPlay()
{
    if (!playing)
        return;

    playing = false;
    uninstallDriver();
}

bool audioIsRecording()
{
    return recording;
}

bool audioIsPlaying()
{
    return playing;
}

bool audioRead(int16_t *buffer, size_t samples)
{
    if (!recording || buffer == nullptr || samples == 0)
        return false;

    static int32_t raw[AUDIO_BLOCK_SIZE * 2];

    size_t requestSamples = samples;
    if (requestSamples > AUDIO_BLOCK_SIZE)
        requestSamples = AUDIO_BLOCK_SIZE;

    size_t bytesRead = 0;
    esp_err_t err = i2s_read(
        I2S_PORT,
        raw,
        requestSamples * 2 * sizeof(int32_t),
        &bytesRead,
        portMAX_DELAY);

    if (err != ESP_OK)
        return false;

    size_t count = bytesRead / (sizeof(int32_t) * 2);
    if (count > requestSamples)
        count = requestSamples;

    for (size_t i = 0; i < count; i++)
    {
        int32_t left = raw[i * 2] >> 13;
        int32_t right = raw[i * 2 + 1] >> 13;
        int32_t sample = MIC_USE_RIGHT_CHANNEL ? right : left;

        static int32_t dc = 0;
        dc += (sample - dc) >> 8;
        sample -= dc;

        if (sample > 32767)
            sample = 32767;
        else if (sample < -32768)
            sample = -32768;

        buffer[i] = (int16_t)sample;
    }

    return count > 0;
}

bool audioWrite(const int16_t *buffer, size_t samples)
{
    if (!playing || buffer == nullptr || samples == 0)
        return false;

    static int16_t stereo[AUDIO_BLOCK_SIZE * 2];
    size_t requestSamples = samples;
    if (requestSamples > AUDIO_BLOCK_SIZE)
        requestSamples = AUDIO_BLOCK_SIZE;

    for (size_t i = 0; i < requestSamples; i++)
    {
        int32_t sample = ((int32_t)buffer[i] * SPEAKER_VOLUME_PERCENT) / 100;

        if (sample > 32767)
            sample = 32767;
        else if (sample < -32768)
            sample = -32768;

        stereo[i * 2] = (int16_t)sample;
        stereo[i * 2 + 1] = (int16_t)sample;
    }

    size_t bytesWritten = 0;
    esp_err_t err = i2s_write(
        I2S_PORT,
        stereo,
        requestSamples * 2 * sizeof(int16_t),
        &bytesWritten,
        portMAX_DELAY);

    if (err != ESP_OK)
        return false;

    return bytesWritten == requestSamples * 2 * sizeof(int16_t);
}

void audioFlush()
{
    if (driverInstalled)
        i2s_zero_dma_buffer(I2S_PORT);
}
