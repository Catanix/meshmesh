#if defined(MM_NRF52)
#pragma GCC optimize("Os")
#endif
#include "People.h"
#if !defined(MM_UI_PREVIEW)
#include <esp_system.h>
#endif
#include "BlobStore.h"
#include "App.h"
#include "I18n.h"
#include "Radar.h"
#include <Mm1Packet.h>
namespace people {
Settings settings;Tally tally;
namespace {constexpr uint32_t Magic=0x4d4d7001,TallyMagic=0x4d4d7401;constexpr unsigned Max=128;
struct Seen {uint32_t hash=0,at=0;int8_t rssi=0;};Seen seen[2][Max];uint32_t fullAt[2]={},lost[2]={},salt=0,saveAt=0;
bool valid(const Settings& s){return s.manual>=0&&s.manual<=999999&&(s.window==30||s.window==60||s.window==300)&&s.rssi>=-100&&s.rssi<=-30;}
bool nameOk(const char* v,size_t n){if(n>NameBytes||!meshmesh::validUtf8((const uint8_t*)v,n))return false;for(size_t i=0;i<n;i++)if(uint8_t(v[i])<32)return false;return true;}
bool valid(const Tally& t){if(!t.count||t.count>Counters||t.current>=t.count)return false;
 for(unsigned i=0;i<t.count;i++){const Counter& c=t.list[i];if(!memchr(c.name,0,sizeof c.name)||!nameOk(c.name,strlen(c.name))||c.value<0||c.value>Limit||c.color>=Colors)return false;}return true;}
uint32_t hash(uint64_t address){uint32_t h=2166136261u^salt;for(unsigned i=0;i<8;i++){h^=uint8_t(address>>(i*8));h*=16777619u;}return h?h:1;}
bool save(const Settings& s){return blob::save("/meshmesh/people",Magic,s);}
bool saveTally(){if(blob::save("/meshmesh/tally",TallyMagic,tally)){saveAt=0;return true;}return false;}
void later(){saveAt=millis()+2000;if(!saveAt)saveAt=1;} // counting: written two seconds after the last press
bool store(){if(!saveTally())later();return true;} // cards added, renamed or removed: at once (retried while storage fails)
String trimmed(const String& name){String v=name;v.trim();return v;}
}
void begin(){Settings saved{};if(blob::load("/meshmesh/people",Magic,saved)&&valid(saved))settings=saved;
 Tally stored{};if(blob::load("/meshmesh/tally",TallyMagic,stored)&&valid(stored))tally=stored;
 else{tally=Tally{};tally.list[0].value=settings.manual;} // the people counter's manual count opens the first card
 resetWindow();}
void resetWindow(){memset(seen,0,sizeof seen);memset(fullAt,0,sizeof fullAt);memset(lost,0,sizeof lost);salt=
#if defined(MM_UI_PREVIEW)
 uint32_t(rand());
#else
 esp_random();
#endif
}
Counter& current(){return tally.list[tally.current<tally.count?tally.current:0];}
int32_t value(){return current().value;}
String name(unsigned i){if(i>=tally.count)return String();if(tally.list[i].name[0])return tally.list[i].name;return String(tr("Counter ","Счётчик "))+String(i+1);}
bool set(unsigned i,int32_t v){if(i>=tally.count||v<0||v>Limit)return false;tally.list[i].value=v;later();return true;}
bool change(unsigned i,int delta){if(i>=tally.count)return false;int64_t v=int64_t(tally.list[i].value)+delta;return v>=0&&v<=Limit&&set(i,v);}
bool change(int delta){return change(tally.current,delta);}
bool select(unsigned i){if(i>=tally.count)return false;if(tally.current!=i){tally.current=i;later();}return true;}
bool add(const String& name){String v=trimmed(name);if(tally.count>=Counters||!nameOk(v.c_str(),v.length()))return false;
 Counter& c=tally.list[tally.count];memset(&c,0,sizeof c);strlcpy(c.name,v.c_str(),sizeof c.name);c.color=tally.count%Colors;tally.current=tally.count++;return store();}
bool rename(unsigned i,const String& name){String v=trimmed(name);if(i>=tally.count||!nameOk(v.c_str(),v.length()))return false;strlcpy(tally.list[i].name,v.c_str(),sizeof tally.list[i].name);return store();}
bool recolor(unsigned i,unsigned color){if(i>=tally.count||color>=Colors)return false;tally.list[i].color=color;return store();}
bool remove(unsigned i){if(i>=tally.count||tally.count<2)return false; // one card always stays
 memmove(&tally.list[i],&tally.list[i+1],sizeof(Counter)*(tally.count-i-1));tally.count--;memset(&tally.list[tally.count],0,sizeof(Counter));
 if(tally.current>i||tally.current>=tally.count)tally.current=tally.current?tally.current-1:0;return store();}
void flush(){if(saveAt)saveTally();}
void hear(bool wifi,uint64_t address,int rssi,bool personal){if(!(wifi?settings.wifi:settings.ble)||rssi<settings.rssi||(!wifi&&settings.personal&&!personal))return;unsigned kind=wifi?1:0;uint32_t now=millis(),id=hash(address);Seen* slot=nullptr;for(auto& entry:seen[kind]){if(entry.hash==id){slot=&entry;break;}if(!slot&&(!entry.hash||now-entry.at>=settings.window*1000u))slot=&entry;}if(!slot){noteDropped(wifi,1);return;}*slot={id,now,int8_t(rssi)};}
unsigned count(bool wifi){unsigned n=0;uint32_t now=millis();for(const auto& entry:seen[wifi?1:0])if(entry.hash&&now-entry.at<settings.window*1000u&&entry.rssi>=settings.rssi)n++;return n;}
void noteDropped(bool wifi,unsigned n){if(!(wifi?settings.wifi:settings.ble)||!n)return;unsigned kind=wifi?1:0;fullAt[kind]=millis()?millis():1;lost[kind]+=n;}
bool saturated(bool wifi){unsigned kind=wifi?1:0;return fullAt[kind]&&millis()-fullAt[kind]<settings.window*1000u;}uint32_t dropped(bool wifi){return saturated(wifi)?lost[wifi?1:0]:0;}
void tick(){if(saveAt&&int32_t(millis()-saveAt)>=0)saveTally();wifiTick();}
String json(){DynamicJsonDocument d(2048);d["manual"]=value();d["current"]=tally.current;d["counters_max"]=Counters;JsonArray cards=d.createNestedArray("counters");
 for(unsigned i=0;i<tally.count;i++){JsonObject o=cards.createNestedObject();o["name"]=tally.list[i].name;o["value"]=tally.list[i].value;o["color"]=tally.list[i].color;}
 d["ble"]=settings.ble;d["wifi"]=settings.wifi;d["personal_only"]=settings.personal;d["window"]=settings.window;d["rssi"]=settings.rssi;d["ble_devices"]=count(false);d["wifi_devices"]=count(true);d["ble_saturated"]=saturated(false);d["wifi_saturated"]=saturated(true);d["ble_dropped"]=dropped(false);d["wifi_dropped"]=dropped(true);d["ble_state"]=!settings.ble?"off":radar.ble==Radar::BleReady?"listening":radar.ble==Radar::BleFailed?"failed":"paused";d["wifi_state"]=wifiState();d["capacity"]=Max;
#if defined(MM_NO_WIFI)
 d["wifi_supported"]=false;
#else
 d["wifi_supported"]=true;
#endif
 String out;serializeJson(d,out);return out;}
// {"action":"change","delta":N[,"index":I]}, "reset"[,index], "set" index value, "select" index, "add"[,name],
// "rename" index name, "color" index color, "delete" index; "window_reset"; "settings" with ble, wifi, personal_only, window, rssi.
String command(JsonObjectConst values){String action=values["action"]|"";
 if(action=="add"){if(tally.count>=Counters)return "ERR 8 counters at most";return add(values["name"]|"")?"OK counter added":"ERR counter name: up to 23 bytes";}
 bool cards=action=="change"||action=="reset"||action=="set"||action=="select"||action=="rename"||action=="color"||action=="delete";
 if(cards){JsonVariantConst at=values["index"];if(!at.isNull()&&!at.is<unsigned>())return "ERR people index";unsigned i=at.isNull()?tally.current:at.as<unsigned>();if(i>=tally.count)return "ERR no such counter";
  if(action=="change"){JsonVariantConst d=values["delta"];if(!d.is<int>()||d.as<int>()<-100||d.as<int>()>100||!change(i,d.as<int>()))return "ERR people counter range";return "OK people counter changed";}
  if(action=="reset"){set(i,0);return "OK people counter reset";}
  if(action=="set")return values["value"].is<long>()&&set(i,values["value"].as<long>())?"OK people counter set":"ERR counter value 0..999999";
  if(action=="select"){select(i);return "OK counter selected";}
  if(action=="rename")return values["name"].is<const char*>()&&rename(i,values["name"].as<const char*>())?"OK counter renamed":"ERR counter name: up to 23 bytes";
  if(action=="color")return values["color"].is<unsigned>()&&recolor(i,values["color"].as<unsigned>())?"OK counter colour":"ERR counter colour 0..14";
  return remove(i)?"OK counter deleted":"ERR the last counter stays";}
 if(action=="window_reset"){resetWindow();return "OK people window reset";}
 Settings next=settings;if(action=="settings"){for(JsonPairConst pair:values){String key=pair.key().c_str();JsonVariantConst v=pair.value();if(key=="action")continue;if(key=="ble"||key=="wifi"||key=="personal_only"){if(!v.is<bool>())return "ERR people boolean";if(key=="ble")next.ble=v;if(key=="wifi")next.wifi=v;if(key=="personal_only")next.personal=v;}else if(key=="window"||key=="rssi"){if(!v.is<int>())return "ERR people integer";int n=v;if(key=="window"){if(n<0||n>300)return "ERR people window";next.window=n;}else{if(n<-100||n>-30)return "ERR people RSSI";next.rssi=n;}}else return "ERR people setting";}}else return "ERR people action change|reset|set|select|add|rename|color|delete|window_reset|settings";
#if defined(MM_NO_WIFI)
 if(next.wifi)return "ERR board has no Wi-Fi";
#endif
 if(!valid(next))return "ERR people: window 30|60|300, RSSI -100..-30";if(!save(next))return "ERR people storage";bool reset=next.window!=settings.window||next.rssi!=settings.rssi||next.personal!=settings.personal;bool stopBle=settings.ble&&!next.ble;settings=next;if(reset)resetWindow();if(stopBle&&!radar.active)radar.releaseBle();if(!next.wifi)releaseWifi();return "OK people settings saved";
}
}
