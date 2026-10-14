#pragma once
// nRF52 (GAT562): MeshMesh storage on the internal flash, 0xD4000-0xED000 (100 KB, littlefs v1
// of the Adafruit core, 128-byte blocks as in its InternalFS). The stock MeshCore InternalFS at
// 0xED000 stays untouched. ESP32-style calls used by the shared code are added here.
#include <Arduino.h>
#include <Adafruit_LittleFS.h>
using namespace Adafruit_LittleFS_Namespace;
#ifndef FILE_READ
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
#endif
class MeshFS:public Adafruit_LittleFS {
 public:
  static constexpr uint32_t Start=0xD4000,Size=0xED000-0xD4000;
  static constexpr const char* marker="/meshmesh.fs"; // written when MeshMesh creates the storage
  MeshFS();
  // Adafruit_LittleFS does not check that it is mounted. After another firmware's littlefs was
  // mounted and ended (mountStorage: WhisperOS, MeshCore), its calls walked that filesystem with the
  // freed buffers and the board restarted at every start. Without storage they fail instead.
  File open(const char* path,uint8_t mode=FILE_O_READ){return _mounted?Adafruit_LittleFS::open(path,mode):File(*this);}
  bool exists(const char* path){return _mounted&&Adafruit_LittleFS::exists(path);}
  bool mkdir(const char* path){return _mounted&&Adafruit_LittleFS::mkdir(path);}
  bool remove(const char* path){return _mounted&&Adafruit_LittleFS::remove(path);}
  bool rename(const char* from,const char* to){return _mounted&&Adafruit_LittleFS::rename(from,to);}
  bool rmdir(const char* path){return _mounted&&Adafruit_LittleFS::rmdir(path);}
  bool rmdir_r(const char* path){return _mounted&&Adafruit_LittleFS::rmdir_r(path);}
  // "r" read, "w" replace, "a" append; ESP32 creates a missing file for "w" and "a" as well.
  File open(const char* path,const char* mode,bool create=false);
  bool begin(bool formatOnFail=false,const char* base=nullptr,uint8_t maxOpen=0,const char* label=nullptr);
  bool format();
  bool blank() const; // every byte of the region is 0xFF (never written or fully erased)
  bool mounted() const{return _mounted;}
};
extern MeshFS LittleFS;
