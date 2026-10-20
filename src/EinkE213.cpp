// Vision Master E213: the 2.13" e-ink panel. Two revisions with different controllers share the board: a
// Fitipower one (LCMEN2R13EFC1, BUSY low while busy) and a Solomon Systech one (E0213A367, BUSY high). Both
// drivers come from heltec-eink-modules, pinned to the commit MeshCore uses; the revision test is MeshCore's
// (E213Display::detectEInk). Only this file includes the library: it brings its own copy of the GFX classes.
// The panel is drawn by a task on core 0: refreshes block on the BUSY pin for 0.5-3 s.
#if defined(MM_PANEL_EINK)
#include "Eink.h"
#include "BoardPins.h"
#include <heltec-eink-modules.h>
namespace {
BaseDisplay* panel=nullptr;
const char* found="none";
uint8_t pending[eink::Stride*eink::Height],drawing[eink::Stride*eink::Height];
volatile bool waiting=false,busy=false;
volatile uint32_t due=0;
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t task=nullptr;
// A partial refresh leaves a trace of the previous image; a full one (black-white flashing) clears it.
constexpr unsigned PartialsPerFull=30;
BaseDisplay* detect(){
  // Held in reset, the controller reports busy: LOW on the Fitipower panel, HIGH on the Solomon one.
  pinMode(pins::einkReset,OUTPUT);digitalWrite(pins::einkReset,LOW);delay(10);
  pinMode(pins::einkBusy,INPUT);bool high=digitalRead(pins::einkBusy);
  pinMode(pins::einkReset,INPUT);
  if(high){found="E0213A367";return new EInkDisplay_VisionMasterE213V1_1;}
  found="LCMEN2R13EFC1";return new EInkDisplay_VisionMasterE213;
}
void draw(const uint8_t* frame){
  for(int y=0;y<eink::Height;y++){const uint8_t* row=frame+y*eink::Stride;
    for(int x=0;x<eink::Width;x++)panel->drawPixel(x,y,row[x>>3]&(0x80>>(x&7))?BLACK:WHITE);}
}
void run(void*){
  unsigned partials=0;
  while(true){
    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(500));
    bool go=false;
    portENTER_CRITICAL(&lock);
    if(waiting&&int32_t(millis()-due)>=0){memcpy(drawing,pending,sizeof drawing);waiting=false;busy=go=true;}
    portEXIT_CRITICAL(&lock);
    if(!go)continue;
    draw(drawing);
    if(++partials>=PartialsPerFull){partials=0;panel->fastmodeOff();panel->update();panel->fastmodeOn();}
    else panel->update();
    busy=false;
  }
}
}
namespace eink {
bool begin(){
  if(panel)return true;
  panel=detect();
  panel->begin();panel->setRotation(3); // landscape, as MeshCore
  panel->clear();                        // a full refresh: whatever the panel kept from before is gone
  panel->fastmodeOn();
  // Priority 0, as the idle task: the library waits for BUSY in a yield() loop, which must not starve it.
  xTaskCreatePinnedToCore(run,"eink",4096,nullptr,0,&task,0);
  return task!=nullptr;
}
void show(const uint8_t* frame,uint32_t notBefore){
  if(!task)return;
  portENTER_CRITICAL(&lock);
  memcpy(pending,frame,sizeof pending);waiting=true;due=notBefore;
  portEXIT_CRITICAL(&lock);
  xTaskNotifyGive(task);
}
bool idle(){return !waiting&&!busy;}
const char* model(){return found;}
}
#endif
