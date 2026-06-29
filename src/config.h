#ifndef CONFIG_H
#define CONFIG_H

// OLED SSD1306 I2C
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C

// PTT button, active LOW with INPUT_PULLUP
#define BUTTON_PIN 18

// WiFi settings for V2 LAN intercom
#define WIFI_SSID "esp32"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// UDP audio broadcast
#define AUDIO_UDP_PORT 4210

// Network mode:
// 1 = same-WiFi LAN broadcast
// 2 = public relay server
#define NETWORK_MODE_LAN 1
#define NETWORK_MODE_RELAY 2
#define NETWORK_MODE NETWORK_MODE_RELAY

// Public UDP relay server
#define RELAY_HOST "8.213.144.39"
#define RELAY_PORT 4210
#define RELAY_KEEPALIVE_MS 1000

// INMP441 microphone
#define MIC_BCLK 26
#define MIC_WS 25
#define MIC_SD 33

// MAX98357A amplifier
#define SPK_BCLK 26
#define SPK_WS 25
#define SPK_DIN 32

// Audio settings
#define SAMPLE_RATE 16000
#define AUDIO_BLOCK_SIZE 512
#define STREAM_GAIN 2
#define AUDIO_NOISE_GATE 260
#define VOICE_SEND_THRESHOLD 650
#define SPEAKER_VOLUME_PERCENT 20

// Mailbox / button behavior
#define MESSAGE_END_TIMEOUT_MS 450
#define BUTTON_TAP_MAX_MS 300
#define BUTTON_LONG_PRESS_MS 800
#define INBOX_IDLE_TIMEOUT_MS 15000

// INMP441: L/R tied to GND usually means left channel.
// Change this to 1 only if the microphone becomes silent.
#define MIC_USE_RIGHT_CHANNEL 0

#endif
