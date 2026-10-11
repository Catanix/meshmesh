#pragma once
#include <Arduino.h>
#include "Board.h"
#include "BoardPins.h"
#if defined(MM_HELTEC_V4)
#include <Adafruit_SSD1306.h>
#elif defined(MM_COMPACT)
#include <Adafruit_GFX.h>
#else
#include <Adafruit_ST7789.h>
#endif
#if defined(MM_HIRES)
#include "HiresCanvas.h"
#endif
#include <U8g2_for_Adafruit_GFX.h>
#include <TinyGPSPlus.h>
#include <RTClib.h>

class Hardware {
 public:
#if defined(MM_HELTEC_V4)
  Adafruit_SSD1306 display;
#elif defined(MM_COMPACT)
  Adafruit_GFX* panel=nullptr; // the board's screen; nullptr when it has none or it did not answer
#else
  Adafruit_ST7789 display;
#endif
#if defined(MM_HIRES)
  HiresCanvas* canvas=nullptr; // 128x64 layout coordinates on the 240x135 TFT
#else
  GFXcanvas16* canvas=nullptr;
#endif
  U8G2_FOR_ADAFRUIT_GFX font;
  TinyGPSPlus gps;
  RTC_PCF8563 rtc;
  bool keyboardOk=false, rtcOk=false, rtcValid=false, sdOk=false, fsOk=false;
  bool compassOk=false, imuOk=false, compassSample=false, imuSample=false;
  float mag[3]={}, accel[3]={};
  uint16_t batteryMv=0;
  uint32_t gpsBytes=0, lastKey=0, keyCount=0, utc=0;
  uint8_t keyboardHw=0, keyboardFw=0;
#if defined(MM_HELTEC_V4)
  Hardware():display(128,64,&Wire,pins::oledReset) {}
#elif defined(MM_COMPACT)
  Hardware() {}
#else
  Hardware():display(&SPI,pins::lcdCs,pins::lcdDc,pins::lcdReset) {}
#endif
  bool clockTrusted=false,clockConflict=false;
  String clockSource="unset";
  void beginClock();
  bool setUtc(uint32_t epoch,const char* source,bool persist=false);
  String clockInfo();
  bool gpsFix();
  uint32_t clockSyncAt=0;uint32_t clockAge() const {return clockSyncAt?uint32_t(millis()-clockSyncAt):UINT32_MAX;}
  bool gpsTime(); // the receiver's date and time may set the clock
  void begin();
  void tick();
  int readKey();
#if defined(MM_BOARD_TDECK)
  char readTouch(int& x,int& y);
#endif
  void brightness(uint8_t level);
  void setGps(bool enabled);
  void beep();
  void ping(uint16_t hz,uint16_t ms);
  void flush();
  // Power off (Power.cpp): the screen, GPS and peripheral supplies off; the levels are held through
  // deep sleep (ESP32) or System OFF (nRF52). started: begin() ran; false on a wake that goes back to
  // sleep, when only the pin levels are set again. A board with a PMU (T-Beam) is switched off by it here.
  void powerDown(bool started);
  int wakePin() const; // the button that turns it on again, pressed = LOW; -1: only RESET or the power switch
  void text(int x,int y,const String& value,uint16_t color=0xffff);
  void line(int y,const String& value,uint16_t color=0xffff);
 private:
  uint32_t lastSample=0, lastProbe=0, lastGpsBaud=0;
  bool gpsEnabled=false;
  bool read(TwoWire& bus,uint8_t addr,uint8_t reg,uint8_t* bytes,uint8_t size,bool stop=false);
  bool write(uint8_t addr,uint8_t reg,uint8_t value);
};
extern Hardware hardware;
// Charge of a Li-ion cell from its voltage, rounded: the same curve as batteryPercent() of web/index.html,
// so the screen, the page and the app agree.
inline unsigned batteryPercent(uint16_t v){
  static const uint16_t mv[]={3300,3500,3600,3700,3800,3900,4000,4100,4200};static const uint8_t pc[]={0,5,15,35,55,70,82,93,100};
  if(v<=mv[0])return 0;for(int i=1;i<9;i++)if(v<=mv[i]){unsigned span=mv[i]-mv[i-1];return pc[i-1]+(2u*(pc[i]-pc[i-1])*(v-mv[i-1])+span)/(2*span);}return 100;}
