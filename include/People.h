#pragma once
#include "Board.h"
#include <ArduinoJson.h>
// The quick counter: up to eight named cards counted by hand, one of them open, and, apart from them, an estimate
// of the BLE and Wi-Fi devices nearby. A card without a name is shown as "Counter N". The "people" names of the
// commands and files stay from the people counter it grew out of; its manual count became the first card.
namespace people {
constexpr unsigned Counters=8,NameBytes=23,Colors=15;constexpr int32_t Limit=999999;
struct Settings {int32_t manual=0;uint16_t window=60;int8_t rssi=-85;bool ble=false,wifi=false,personal=false;}; // manual: before 0.24.0
struct Counter {char name[NameBytes+1];int32_t value;uint8_t color;};
struct Tally {uint8_t count=1,current=0;Counter list[Counters]={};};
extern Settings settings;extern Tally tally;
void begin();void tick();void flush();void resetWindow();
Counter& current();int32_t value(); // the open card and its value
String name(unsigned i); // its name, or "Counter N"
bool change(int delta); // the open card, within 0..Limit
bool change(unsigned i,int delta);bool set(unsigned i,int32_t value);bool select(unsigned i);
bool add(const String& name=String());bool rename(unsigned i,const String& name);bool recolor(unsigned i,unsigned color);bool remove(unsigned i);
void hear(bool wifi,uint64_t address,int rssi,bool personal=true);
void noteDropped(bool wifi,unsigned count);unsigned count(bool wifi);bool saturated(bool wifi);uint32_t dropped(bool wifi);
String json();String command(JsonObjectConst values);
void wifiTick();void releaseWifi();bool wifiRunning();const char* wifiState();
}
