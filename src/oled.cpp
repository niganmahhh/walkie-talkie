#include "oled.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

static Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1);

OLED oled;

static bool networkConnected = false;
static String networkIp = "0.0.0.0";

bool OLED::begin()
{
    Wire.begin(OLED_SDA, OLED_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    {
        return false;
    }

    display.clearDisplay();
    display.display();

    return true;
}

void OLED::clear()
{
    display.clearDisplay();
    display.display();
}

void OLED::setNetworkInfo(bool connected, const String &ip)
{
    networkConnected = connected;
    networkIp = ip;
}

void OLED::showText(const String &line1, const String &line2)
{
    display.clearDisplay();

    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(line1);

    display.setTextSize(1);
    display.setCursor(0, 36);
    display.println(line2);

    display.display();
}

void OLED::showReady()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("==================");

    display.setCursor(32, 10);
    display.println("ESP TALK");

    display.setCursor(0, 22);
    display.print("WiFi : ");
    display.println(networkConnected ? "Connected" : "Offline");

    display.setCursor(0, 32);
    display.print("IP   : ");
    display.println(networkIp);

    display.setCursor(0, 44);
    display.println("Status:");

    display.setCursor(0, 54);
    display.println("Waiting...");

    display.display();
}

void OLED::showRecording()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("==================");

    display.setCursor(32, 14);
    display.println("ESP TALK");

    display.setTextSize(2);
    display.setCursor(10, 36);
    display.println("Talking...");

    display.display();
}

void OLED::showPlaying()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("==================");

    display.setCursor(32, 14);
    display.println("ESP TALK");

    display.setTextSize(2);
    display.setCursor(4, 36);
    display.println("Receiving...");

    display.display();
}
