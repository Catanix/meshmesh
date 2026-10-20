#pragma once
// Vision Master E213 e-ink panel (EinkE213.cpp). A frame is 250x122 pixels, one bit each, rows of 32 bytes,
// most significant bit first, 1 = black. The panel is redrawn by a task of its own: a partial refresh takes
// about half a second and a full one a few seconds, and the main loop (radio, USB, BLE) does not wait for them.
#include <Arduino.h>
namespace eink {
constexpr int Width=250,Height=122,Stride=32;
bool begin();                                   // finds the panel revision, clears it; false: no panel answered
void show(const uint8_t* frame,uint32_t notBefore); // the newest frame wins; drawn no earlier than millis() notBefore
bool idle();                                    // nothing waiting or being drawn
const char* model();                            // the controller found by begin()
}
