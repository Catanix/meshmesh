#pragma once
#include "Modules.h"
#include <Arduino.h>
// Dice roller, the port of DIC3R (docs/dice.md): RPG rolls a pool ("2d6+1": count, die, modifier) or a formula
// ("3d8+2d4-1"; "2к6" in Russian notation; d66 and d% show each die, without a sum); the results keep the last
// eight rolls. The Warhammer grid rolls the first N of its dice at once, sorted from high to low, and with a
// success threshold ("4+") counts the dice that pass it. The third mode is the character sheet: blocks in rows
// (stats in shapes, tracks with a maximum, pips, abilities with a roll and a description, notes, headers).
// A stat follows a rule: D&D gives the modifier of its score, R.E.T. the die of its level (exploding upwards),
// a bonus is added to d20. Values and rolls refer to other blocks by code: "10+@dex", "d20+@dex+@pb".
// Character 0 is the general set. The device keeps it all (files in MeshMesh storage), so the screens, the web
// page and the app show the same; the hardware generator rolls the dice. A sheet travels to a contact as
// acknowledged direct messages and waits there until accepted.
namespace dice {
enum Mode:uint8_t {Rpg,Grid,Sheet,ModeCount};
enum Type:uint8_t {D4,D6,D8,D10,D12,D20,D66,DPercent,TypeCount};
constexpr unsigned Characters=6,Colors=15,Results=8,Parts=24,Terms=8;
constexpr unsigned GridStart=20,GridRow=5,GridMax=50,MaxCount=99,NameBytes=23,FormulaBytes=27;
constexpr int MaxMod=99,CounterStart=20,CounterLimit=9999;
extern const char* const typeNames[TypeCount];               // "d4" ... "d66", "d%"
// The palette of the app's counters and saved rolls (DIC3R colorPalette), as 0xRRGGBB.
constexpr uint32_t colors[Colors]={0xff6b6b,0x4ecdc4,0x45b7d1,0xffa07a,0x98d8c8,0xf06292,0xaed581,0xffd54f,0x4dd0e1,0xba68c8,0x4fc3f7,0xfff176,0x81c784,0xdce775,0x64b5f6};
unsigned faces(uint8_t type);                                // the largest value: 6 for d6, 66 for d66, 100 for d%
String valueText(uint8_t type,unsigned v);                   // d% shows 100 as "00" and 1..9 as "01".."09"
// The character sheet. Widths are parts of a row of twelve columns: a sixth, a quarter, a third, a half, all.
enum Kind:uint8_t {Stat,Track,Pips,Ability,Note,Header,KindCount};
enum Shape:uint8_t {Circle,Square,Shield,Diamond,Hexagon,ShapeCount};
enum Rule:uint8_t {Plain,Dnd,Bonus,Ret,RuleCount};          // Bonus: the value is a modifier, a check is d20+value
enum Width:uint8_t {Sixth,Quarter,Third,Half,Whole,WidthCount};
constexpr unsigned Blocks=64,CodeBytes=7,BlockName=39,BlockFormula=31,NoteBytes=200,NotesBytes=2048,PipsMax=12,RetTop=8;
constexpr int StatLimit=999;
extern const char* const kindNames[KindCount];               // "stat" ... "header" (commands, JSON)
unsigned span(uint8_t width);                                // columns of twelve: 2, 3, 4, 6, 12
// value/max by kind: a stat's value (its formula, when set, derives it); a track's current value and maximum
// (0: none; the formula derives it); pips marked of max. An ability rolls its formula and takes `spend` from
// the block `use`. The note (abilities, notes) lies in SheetData::notes.
struct Block{
  uint8_t kind,width,shape,color,rule,spend;
  int16_t value,max;
  uint16_t note,noteLen;
  char code[CodeBytes+1],use[CodeBytes+1],name[BlockName+1],formula[BlockFormula+1];
};
struct SheetData{uint8_t count;uint16_t notesUsed;Block block[Blocks];char notes[NotesBytes];};
enum Template:uint8_t {Blank,DndTemplate,RetTemplate,TemplateCount};
extern const char* const templateNames[TemplateCount];       // "empty", "dnd", "ret"
struct Character{char name[NameBytes+1];uint8_t file;};     // file: the number of its sheet file
struct State{
  uint32_t version=2;
  uint8_t character=0,characters=1,mode=Rpg;
  uint8_t type=D6,count=2;int8_t mod=0;                      // the RPG pool
  uint8_t gridType=D6,gridSize=GridStart,gridChosen=0,threshold=4;bool thresholdOn=false;
  Character list[Characters]={};
  uint32_t check=0;
};
// A roll: its parts in order (dice, numbers, special dice) and the sum of the ordinary ones. A R.E.T. die
// counts its successes (each top value adds the die of the next level); a 1 on it is a failure, the d4 of
// failure tells a critical one (1) and the bonuses are dropped.
enum Part:uint8_t {PartDie,PartNumber,PartD66,PartPercent};
struct Result{
  char label[BlockName+1];char formula[FormulaBytes+1];
  uint8_t parts;uint16_t value[Parts];uint8_t kind[Parts],shape[Parts];bool negative[Parts]; // shape: the Type the screens draw, TypeCount for other dice
  bool more,special;                                         // more: dice beyond the shown parts; special: d66/d% (no sum)
  uint8_t successes,fail;                                    // R.E.T.: successes; fail: the d4 of failure, 0 without one
  int32_t total;uint32_t serial;
};
// A sheet on its way: sent part by part to a contact, or received from one.
struct Outgoing{uint64_t peer;char name[25];uint16_t id;uint8_t part,parts,tries;uint32_t msg,at;String text;bool failed,done;};
struct Offer{uint64_t peer;char from[25],name[NameBytes+1];String text;};
void copyText(char* to,size_t size,const char* from); // strlcpy that cuts between UTF-8 characters
String readable(const char* formula);                 // R.E.T. dice as they read: "r5+3" is "d10+3", "r1" is "d6-2"
class Dicer {
 public:
  // On the heap, made at start-up: the static RAM of the ESP32 boards without PSRAM has no room for them.
  State& s=*new State;
  SheetData& sheet=*new SheetData;                           // the current character's sheet
  Result* const results=new Result[Results];unsigned resultCount=0;uint32_t rolls=0; // newest last
  uint8_t grid[GridMax]={};bool gridRolled=false;            // the values on the grid; rolled: the chosen ones are fresh
  uint32_t events=0,sheetRev=0;                              // events: every change a screen should show; sheetRev: of the sheet
  Outgoing out={};Offer offer={};bool offered=false;String note;uint32_t notes=0; // note: news of a transfer
  String why;                                                // why the last sheet action was refused
  void begin();void tick();void flush(){if(saveDue)save();}
  Character& hero(){return s.list[s.character];}
  const Character& hero() const{return s.list[s.character];}
  String heroName(uint8_t i) const;                          // "General" for character 0 without a name
  String poolText() const;                                   // "2d6+1"
  // Rolls. A formula: terms joined by + and -, each NdX (N up to 99, X 2..1000, d% d66), dX, rN (the R.E.T. die
  // of level N, 0..8) or a number.
  bool roll(const String& formula,const String& label=String());
  bool rollPool(){return roll(poolText());}
  void setPool(int count,int type,int mod);
  static bool valid(const String& formula);
  static String clean(const String& formula);                // lower case, no spaces, "к" and "д" as "d"
  const Result* last() const{return resultCount?&results[resultCount-1]:nullptr;}
  String resultText(const Result& r,bool sum=true) const;    // "4 + 3 + 1 = 8"
  void clearResults(){resultCount=0;events++;}
  // The Warhammer grid.
  void gridChoose(int n);void gridRoll();void gridAddRow();void gridReset();void gridSetType(int type);void gridSetThreshold(bool on,int value);
  unsigned gridSuccesses() const;
  // Saved rolls: the sheet's abilities with a formula (the RPG mode lists them).
  unsigned savedCount() const;int savedBlock(unsigned i) const; // the block of saved roll i, -1 when missing
  bool rollSaved(unsigned i);
  bool addRoll(const String& formula,const String& name);bool editRoll(unsigned i,const String& formula,const String& name);
  bool deleteRoll(unsigned i);bool colorRoll(unsigned i,unsigned color);
  // The sheet. Indexes are block positions; false when missing, full or invalid.
  int find(const char* code) const;                         // the block with this code, -1
  int value(unsigned i) const;                              // a stat's or pips' value, a track's current one (derived: computed)
  int maxOf(unsigned i) const;                              // a track's or pips' maximum (derived: computed)
  bool derivedOk(unsigned i) const;                         // its formula computes
  int number(unsigned i) const;                             // what @code gives: the D&D modifier, else the value
  String badge(unsigned i) const;                           // under a stat: "+3" (D&D), "d8" (R.E.T.), empty
  String rollFormula(unsigned i) const;                     // the roll with the codes put in: "d20+5", "r4+3"; empty: no roll
  String actName(unsigned i) const;                         // what OK does: "Roll", "Use", ... empty when nothing
  String noteText(unsigned i) const;
  bool act(unsigned i);                                     // roll a check or an ability (and spend), mark pips
  bool change(unsigned i,int delta);bool setValue(unsigned i,int v);
  int addBlock(uint8_t kind,int at=-1);                     // the new block's index
  bool setField(unsigned i,const String& field,const String& v,String* error=nullptr);
  bool moveBlock(unsigned i,unsigned to);bool deleteBlock(unsigned i);
  bool applyTemplate(uint8_t t);                            // replaces the current sheet
  String exportText() const;                                // one line: "MMS1~N|name~kind|..."
  bool importText(const String& text,String* error=nullptr); // a new character
  String sheetJson() const;
  // Characters: up to six, each with its sheet; the general set (0) cannot be removed.
  bool addCharacter(const String& name,uint8_t tmpl=Blank);bool useCharacter(unsigned i);bool renameCharacter(const String& name);bool deleteCharacter();
  void setMode(uint8_t mode);
  // Sending the sheet to a contact, and the sheet a contact sent.
  bool send(uint64_t peer,String* error=nullptr);
  bool receive(uint64_t from,const char* name,const char* text); // from MeshRadio: true when it was a sheet part
  bool delivery(uint32_t id,uint8_t status);                // a part's ACK; true when it was one
  bool acceptOffer();void declineOffer();
  bool migrate(const uint8_t* stored,size_t size);           // dice.bin of 0.7-0.24: counters and saved rolls become blocks
  String json() const;
  String command(const String& line);                        // USB "dice ..."
  // The computation behind value(), maxOf() and number(): depth counts the references followed (a loop runs out of it).
  int valueAt(unsigned i,unsigned depth,bool& ok) const;int maxAt(unsigned i,unsigned depth,bool& ok) const;int numberAt(unsigned i,unsigned depth,bool& ok) const;
 private:
  uint32_t saveDue=0;
  String upload;uint8_t uploadNext=0;                        // "sheet put": a sheet that came in pieces (BLE writes take 255 bytes)
  struct Incoming{uint64_t peer;char from[25];uint16_t id;uint8_t parts;uint32_t at;char* buf;uint8_t len[72];}; // len 0: the part is missing
  Incoming* in=nullptr;
  void changed(bool soon=false);                             // soon: values, written two seconds after the last press
  void sheetChanged(bool soon=false){sheetRev++;changed(soon);}
  void save();
  bool loadSheet(uint8_t file);void saveSheet();
  bool parseSheet(const String& text,SheetData& out,String& name,String* error) const;
  bool setNote(SheetData& d,unsigned i,const String& v) const;
  bool resolve(const char* f,String& out,bool strict) const;
  void sendNext();
};
}
extern dice::Dicer dicer;
