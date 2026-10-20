// fakeTec / ProMicro nRF52840 DIY (nRF52840 + SX1262 module): an optional 0.96" SSD1306 OLED on I2C, an
// optional button to GND, an optional GPS on Serial1. Without the OLED the board works through the app
// (BLE, USB); without the button the boot-time role choice and the screen are simply not used.
#if defined(MM_BOARD_FAKETEC)
#include "Hardware.h"
#include "Config.h"
#include "OledLevel.h"
#include <LittleFS.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <esp_system.h>
Hardware hardware;
namespace {
Adafruit_SSD1306 oled(128,64,&Wire,pins::oledReset);
bool uartOn=false;
bool probe(uint8_t address) {Wire.beginTransmission(address);return Wire.endTransmission()==0;}
void output(int pin,int level){if(pin<0)return;pinMode(pin,OUTPUT);digitalWrite(pin,level);}
}
void Hardware::brightness(uint8_t level) {
  if(!panel)return;if(!level){oled.ssd1306_command(SSD1306_DISPLAYOFF);return;}
  oled.ssd1306_command(SSD1306_DISPLAYON);oledLevel(level,false,[](uint8_t c){oled.ssd1306_command(c);});
}
void Hardware::setGps(bool enabled) {
  gpsEnabled=enabled;
  output(pins::gpsEnable,enabled?pins::gpsOn:!pins::gpsOn);
  // As on the GAT562: Uart::end() of a port never started waits forever, so start and stop it once.
  if(enabled&&!uartOn){Serial1.setPins(pins::gpsRx,pins::gpsTx);Serial1.begin(9600);uartOn=true;}
  else if(!enabled&&uartOn){Serial1.end();uartOn=false;}
}
void Hardware::begin() {
  output(pins::radioPower,HIGH);delay(10); // SX1262 module supply
  output(pins::radioCs,HIGH);
  SPI.setPins(pins::spiMiso,pins::spiClock,pins::spiMosi);SPI.begin();
  Wire.setPins(pins::sda,pins::scl);Wire.begin();Wire.setClock(400000);
  uint8_t address=probe(0x3c)?0x3c:probe(0x3d)?0x3d:0;
  if(address&&oled.begin(SSD1306_SWITCHCAPVCC,address,true,false))panel=&oled;
  if(panel){oled.clearDisplay();oled.display();}
  brightness(config.brightness);
  canvas=new GFXcanvas16(128,64);if(!canvas||!canvas->getBuffer()){Serial.println("FATAL framebuffer");while(true)delay(1000);}
  font.begin(*canvas);font.setFont(u8g2_font_6x13_t_cyrillic);font.setFontMode(1);canvas->fillScreen(0);text(0,13,"MeshMesh");flush();
  pinMode(pins::button,INPUT_PULLUP);
  output(pins::led,!pins::ledOn);pinMode(pins::battery,INPUT);
  setGps(config.gps);
  fsOk=LittleFS.mounted(); // mounted by main.cpp before the settings were read
  Serial.printf("HW %s screen=%d fs=%d heap=%u\n",MM_BOARD_NAME,panel!=nullptr,fsOk,ESP.getFreeHeap());
}
void Hardware::tick() {
  if(gpsEnabled){unsigned budget=512;while(Serial1.available()&&budget--){gps.encode(Serial1.read());gpsBytes++;}}
  if(millis()-lastSample<1000)return;lastSample=millis();
  batteryMv=batteryNow();
  if(gpsTime()) {
    DateTime now(gps.date.year(),gps.date.month(),gps.date.day(),gps.time.hour(),gps.time.minute(),gps.time.second());setUtc(now.unixtime(),"GPS");
  }
}
// Divider on AIN7 (P0.31), as MeshCore's PromicroBoard: 12 bits against the default 3.6 V reference.
uint16_t Hardware::batteryNow() {
  analogReference(AR_DEFAULT);analogReadResolution(12);uint32_t raw=0;for(int i=0;i<8;i++)raw+=analogRead(pins::battery);
  return uint16_t(pins::batteryScale*(raw/8));
}
int Hardware::readKey() {
  static bool held=false,longSent=false,veryLongSent=false;static uint32_t down=0;
  bool pressed=digitalRead(pins::button)==LOW;
  if(pressed && !held) {held=true;longSent=veryLongSent=false;down=millis();}
  if(pressed && held && !longSent && millis()-down>=800) {longSent=true;keyCount++;lastKey=0xa3;return 0xa3;}
  if(pressed && held && !veryLongSent && millis()-down>=5000) {veryLongSent=true;keyCount++;lastKey=0xa4;return 0xa4;} // still held at 5 s (after 0xa3)
  if(!pressed && held) {held=false;if(!longSent && millis()-down>30){keyCount++;lastKey=13;return 13;}}
  return 0;
}
// Off: the OLED, GPS and LED off, the SX1262 asleep and deselected (its module supply stays: some fakeTec
// builds feed it straight from the battery). System OFF keeps the levels; the button wakes it.
void Hardware::powerDown(bool started) {
  if(started){brightness(0);setGps(false);}
  output(pins::gpsEnable,!pins::gpsOn);output(pins::led,!pins::ledOn);output(pins::radioCs,HIGH);
}
int Hardware::wakePin() const {return pins::button;}
void Hardware::beep() {}
void Hardware::ping(uint16_t,uint16_t) {}
void Hardware::flush() {
  if(!panel)return;oled.clearDisplay();for(int y=0;y<64;y++)for(int x=0;x<128;x++)if(canvas->getPixel(x,y))oled.drawPixel(x,y,1);oled.display();
}
void Hardware::text(int x,int y,const String& value,uint16_t color) {font.setForegroundColor(color);font.setCursor(x,y);font.print(value);}
void Hardware::line(int y,const String& value,uint16_t color) {text(0,y,value,color);}
#endif
