#if defined(MM_NRF52)
#pragma GCC optimize("Os") // 1 MB flash: the dice are not speed-critical (the rest of the nRF52 image is -O2)
#endif
#include "Dice.h"
#include "MeshRadio.h"
#include "Hardware.h"
#include "I18n.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Mm1Packet.h>
#if !defined(MM_UI_PREVIEW)
#include <esp_system.h>
#endif
#if MM_DICE
dice::Dicer dicer;
namespace dice {
const char* const typeNames[TypeCount]={"d4","d6","d8","d10","d12","d20","d66","d%"};
const char* const kindNames[KindCount]={"stat","track","pips","ability","note","header"};
const char* const templateNames[TemplateCount]={"empty","dnd","ret"};
void copyText(char* to,size_t size,const char* from){size_t n=strlen(from);if(n>=size){n=size-1;while(n&&(uint8_t(from[n])&0xc0)==0x80)n--;}memcpy(to,from,n);to[n]=0;}
unsigned span(uint8_t width){static const uint8_t s[WidthCount]={2,3,4,6,12};return width<WidthCount?s[width]:12;}
namespace {
const char* const DiceFile="/meshmesh/dice.bin";const char* const DiceTemp="/meshmesh/dice.tmp";
const char Tag[]="\xf0\x9f\x8e\xb2"; // 🎲 starts every part of a sheet sent to a contact
constexpr unsigned Chunk=120,MaxParts=72,SheetFiles=8;constexpr uint32_t SheetMagic=0x3153534d; // "MSS1"
#if defined(MM_UI_PREVIEW)
uint32_t entropy(){return rand();}
bool fsOk(){return true;}
#else
uint32_t entropy(){return esp_random();} // the hardware generator
bool fsOk(){return hardware.fsOk;}
#endif
// 1..n without the bias of a plain modulo.
unsigned uniform(unsigned n){uint32_t limit=UINT32_MAX-UINT32_MAX%n,v;do v=entropy();while(v>=limit);return 1+v%n;}
uint32_t fnv(const uint8_t* p,size_t n,uint32_t h=2166136261u){while(n--)h=(h^*p++)*16777619u;return h;}
#define t(en,ru) String(tr(en,ru)) // a macro: tr() keys its translation at compile time (I18n.h)
bool fits(const String& v,unsigned bytes){return v.length()&&v.length()<=bytes&&meshmesh::validUtf8((const uint8_t*)v.c_str(),v.length());}
bool oneLine(const String& v){for(unsigned i=0;i<v.length();i++)if(uint8_t(v[i])<32)return false;return true;}
String sign(int v){return v<0?String(v):"+"+String(v);}
int floorDiv(long a,long b){long q=a/b;if((a%b)&&((a<0)!=(b<0)))q--;return q;}
int clampInt(long v,long lo,long hi){return v<lo?lo:v>hi?hi:v;}
// Codes: a Latin letter, then letters, digits and "_", 7 at most, lower case.
bool codeOk(const String& c){if(!c.length()||c.length()>CodeBytes||!isalpha((unsigned char)c[0]))return false;for(unsigned i=0;i<c.length();i++){char x=c[i];if(!isalnum((unsigned char)x)&&x!='_')return false;}return true;}
String lower(String v){v.toLowerCase();return v;}
// The R.E.T. dice by level: d6-3, d6-2, d6-1 (penalty levels), d6, d8, d10, d12, d20, d100.
unsigned retFaces(unsigned level){static const uint8_t f[RetTop+1]={6,6,6,6,8,10,12,20,100};return f[min(level,RetTop)];}
int retPenalty(unsigned level){return level<3?int(level)-3:0;}
String retDie(unsigned level){String v="d"+String(retFaces(level));int p=retPenalty(level);if(p)v+=String(p);return v;}
// One term of a formula: count dice of `sides` (66: d66, 100 with percent: d%; the level of a R.E.T. die) or a number.
struct Term{bool negative,die;uint8_t count,special;uint16_t sides;int32_t number;};
enum Special:uint8_t {Ordinary,Sixty,Percent,RetDie};
unsigned parse(const String& f,Term* terms){
  unsigned n=0,i=0;const char* p=f.c_str();unsigned len=f.length();
  if(!len||len>FormulaBytes)return 0;
  while(i<len){
    if(n>=Terms)return 0;Term& v=terms[n];v={};
    if(p[i]=='+'||p[i]=='-'){v.negative=p[i]=='-';i++;}else if(n)return 0; // terms after the first need a sign
    uint32_t a=0;unsigned digits=0;while(i<len&&isdigit((unsigned char)p[i])&&digits<5){a=a*10+p[i]-'0';i++;digits++;}
    if(i<len&&p[i]=='r'){if(digits||i+1>=len||!isdigit((unsigned char)p[i+1])||p[i+1]-'0'>int(RetTop))return 0;v.die=true;v.count=1;v.special=RetDie;v.sides=p[i+1]-'0';i+=2;}
    else if(i<len&&p[i]=='d'){
      i++;v.die=true;if(digits&&(a<1||a>MaxCount))return 0;v.count=digits?a:1;
      if(i<len&&p[i]=='%'){i++;v.special=Percent;v.sides=100;}
      else{uint32_t b=0;unsigned bd=0;while(i<len&&isdigit((unsigned char)p[i])&&bd<5){b=b*10+p[i]-'0';i++;bd++;}
        if(!bd||b<2||b>1000)return 0;v.sides=b;if(b==66)v.special=Sixty;}
    }else{if(!digits||a>CounterLimit)return 0;v.number=a;}
    if(i<len&&isdigit((unsigned char)p[i]))return 0; // a number too long
    n++;
  }
  return n;
}
// The dice of a formula with its numbers summed at the end: "d20+3+2" is "d20+5".
String compact(const String& f){
  Term terms[Terms];unsigned n=parse(f,terms);if(!n)return f;String v;long sum=0;
  for(unsigned k=0;k<n;k++){const Term& x=terms[k];if(!x.die){sum+=x.negative?-x.number:x.number;continue;}
    if(x.negative)v+='-';else if(v.length())v+='+';
    if(x.special==RetDie)v+="r"+String(x.sides);else{if(x.count>1)v+=String(x.count);v+=x.special==Percent?String("d%"):"d"+String(x.sides);}}
  if(sum||!v.length())v+=v.length()&&sum>=0?"+"+String(sum):String(sum);
  return v;
}
// The derived values: integers with + - * / (rounded down) and brackets, numbers and @code (@code! raw).
struct Expr{
  const Dicer& d;const char* p;unsigned depth;bool loose;bool ok=true;
  void skip(){while(*p==' ')p++;}
  long ref(){char c[CodeBytes+1];unsigned n=0;while(isalnum((unsigned char)*p)||*p=='_'){if(n<CodeBytes)c[n++]=tolower(*p);else ok=false;p++;}c[n]=0;
    bool raw=*p=='!';if(raw)p++;if(!n){ok=false;return 0;}if(loose)return 1;
    int i=d.find(c);if(i<0||depth>8){ok=false;return 0;}const Block& b=d.sheet.block[i];bool good=true;long v;
    if(!raw)v=b.kind==Stat?d.numberAt(i,depth,good):d.valueAt(i,depth,good);
    else v=b.kind==Stat?d.valueAt(i,depth,good):d.maxAt(i,depth,good);
    ok&=good;return v;}
  long primary(){skip();
    if(*p=='('){p++;long v=sum();skip();if(*p==')')p++;else ok=false;return v;}
    if(*p=='-'){p++;return -primary();}
    if(*p=='@'){p++;return ref();}
    if(isdigit((unsigned char)*p)){long v=0;unsigned k=0;while(isdigit((unsigned char)*p)){v=v*10+(*p-'0');p++;if(++k>5)ok=false;}return v;}
    ok=false;return 0;}
  long product(){long v=primary();for(;;){skip();if(*p=='*'){p++;v=clampInt(v*primary(),-1000000,1000000);}else if(*p=='/'){p++;long b=primary();if(!b){if(!loose)ok=false;return 0;}v=floorDiv(v,b);}else return v;}}
  long sum(){long v=product();for(;;){skip();if(*p=='+'){p++;v+=product();}else if(*p=='-'){p++;v-=product();}else return v;}}
  long all(){long v=sum();skip();if(*p)ok=false;return v;}
};
// A sheet as text, one line (commands and messages): "MMS1~N|name~kind|width|shape|colour|rule|code|name|value|max|formula|use|spend|note~...".
// Zeros and empty fields are left empty, trailing ones out; "\" escapes "\", "|", "~" and a line break.
String escape(const char* v,size_t n){String o;for(size_t i=0;i<n&&v[i];i++){char c=v[i];if(c=='\\'||c=='|'||c=='~'){o+='\\';o+=c;}else if(c=='\n')o+="\\n";else o+=c;}return o;}
String num(int v){return v?String(v):String();}
// Splits at an unescaped separator; the escapes stay (unescape later).
unsigned split(const String& s,char sep,String* out,unsigned most){
  unsigned n=0;String cur;for(unsigned i=0;i<s.length();i++){char c=s[i];
    if(c=='\\'&&i+1<s.length()){cur+=c;cur+=s[++i];continue;}
    if(c==sep){if(n<most)out[n]=cur;n++;cur="";continue;}cur+=c;}
  if(n<most)out[n]=cur;return n+1;
}
String unescape(const String& s){String o;for(unsigned i=0;i<s.length();i++){char c=s[i];if(c=='\\'&&i+1<s.length()){char e=s[++i];o+=e=='n'?'\n':e;}else o+=c;}return o;}
String jsonText(const char* v,size_t n=SIZE_MAX){
  String o="\"";for(size_t i=0;i<n&&v[i];i++){uint8_t c=v[i];if(c=='"'||c=='\\'){o+='\\';o+=char(c);}else if(c=='\n')o+="\\n";else if(c<32){char b[8];snprintf(b,sizeof b,"\\u%04x",c);o+=b;}else o+=char(c);}
  return o+"\"";
}
String sheetPath(uint8_t file,bool temp){return String("/meshmesh/sheet")+String(file)+(temp?".tmp":".bin");}
// The sheet files: a header, the blocks in use and the notes in use; written to a temporary file, then renamed.
struct SheetHead{uint32_t magic;uint16_t count,notes;uint32_t check;};
uint32_t sheetCheck(const SheetData& d){uint32_t h=fnv((const uint8_t*)d.block,sizeof(Block)*d.count);return fnv((const uint8_t*)d.notes,d.notesUsed,h);}
bool blockOk(const Block& b,unsigned notesUsed){
  return b.kind<KindCount&&b.width<WidthCount&&b.shape<ShapeCount&&b.color<Colors&&b.rule<RuleCount&&memchr(b.code,0,sizeof b.code)&&memchr(b.use,0,sizeof b.use)
    &&memchr(b.name,0,sizeof b.name)&&memchr(b.formula,0,sizeof b.formula)&&b.noteLen<=NoteBytes&&b.note+b.noteLen<=notesUsed;
}
void blank(SheetData& d){d.count=0;d.notesUsed=0;}
// The V1 file (0.7.0-0.24.x): saved rolls and counters of each character; the layout of that time.
struct V1Roll{char name[NameBytes+1];char formula[FormulaBytes+1];uint8_t color;};
struct V1Counter{char name[NameBytes+1];int16_t value;uint8_t color;};
struct V1Character{char name[NameBytes+1];uint8_t rolls,counters;V1Roll roll[10];V1Counter counter[6];};
struct V1State{uint32_t version;uint8_t character,characters,mode;uint8_t type,count;int8_t mod;uint8_t gridType,gridSize,gridChosen,threshold;bool thresholdOn;V1Character list[Characters];uint32_t check;};
// The V2 file (0.25.0): State without its flags.
struct V2State{uint32_t version;uint8_t character,characters,mode;uint8_t type,count;int8_t mod;uint8_t gridType,gridSize,gridChosen,threshold;bool thresholdOn;Character list[Characters];uint32_t check;};
}
String readable(const char* f){
  String v;for(const char* p=f;*p;p++){if(*p=='r'&&p[1]>='0'&&p[1]<='0'+int(RetTop)&&(p==f||!isalnum((unsigned char)p[-1]))){v+=retDie(p[1]-'0');p++;}else v+=*p;}
  return v;
}
unsigned faces(uint8_t type){static const uint8_t f[TypeCount]={4,6,8,10,12,20,66,100};return type<TypeCount?f[type]:6;}
String valueText(uint8_t type,unsigned v){if(type!=DPercent)return String(v);if(v>=100)return "00";char b[4];snprintf(b,sizeof b,"%02u",v);return b;}
String Dicer::heroName(uint8_t i) const{if(i>=s.characters)return String();return s.list[i].name[0]?String(s.list[i].name):t("General","Общий");}
String Dicer::poolText() const{String v=String(s.count)+typeNames[s.type<TypeCount?s.type:D6];if(s.mod>0)v+="+"+String(s.mod);else if(s.mod<0)v+=String(s.mod);return v;}
String Dicer::clean(const String& formula){
  String v;for(unsigned i=0;i<formula.length();i++){uint8_t c=formula[i];
    if(c==0xd0&&i+1<formula.length()){uint8_t d=formula[i+1];if(d==0xba||d==0x9a||d==0xb4||d==0x94){v+='d';i++;continue;}} // к К д Д
    if(c==' '||c=='\t'){unsigned j=i;while(j<formula.length()&&(formula[j]==' '||formula[j]=='\t'))j++; // "2d6 3" stays apart (and invalid)
      if(v.length()&&j<formula.length()&&isalnum((unsigned char)v[v.length()-1])&&isalnum((unsigned char)formula[j]))v+=' ';i=j-1;continue;}
    v+=char(c>='A'&&c<='Z'?c+32:c);}
  return v;
}
bool Dicer::valid(const String& formula){Term terms[Terms];return parse(clean(formula),terms)>0;}
bool Dicer::roll(const String& formula,const String& label){
  String f=clean(formula);Term terms[Terms];unsigned n=parse(f,terms);if(!n)return false;
  if(resultCount==Results){memmove(results,results+1,sizeof(Result)*(Results-1));resultCount--;}
  Result& r=results[resultCount];memset(&r,0,sizeof r);
  strlcpy(r.formula,f.c_str(),sizeof r.formula);if(label.length())copyText(r.label,sizeof r.label,label.c_str());
  auto push=[&](uint16_t v,uint8_t kind,bool neg,uint8_t shape){if(r.parts<Parts){r.value[r.parts]=v;r.kind[r.parts]=kind;r.shape[r.parts]=shape;r.negative[r.parts]=neg;r.parts++;}else r.more=true;};
  auto shapeOf=[](unsigned sides){for(uint8_t i=0;i<D66;i++)if(faces(i)==sides)return i;return uint8_t(TypeCount);};
  int32_t failed=0;
  for(unsigned k=0;k<n;k++){const Term& v=terms[k];
    if(!v.die){push(v.number,PartNumber,v.negative,TypeCount);r.total+=v.negative?-v.number:v.number;continue;}
    if(v.special==RetDie){
      // A R.E.T. check: the top value is a success and adds the die of the next level (up to d100); a 1 on
      // the first die is a failure, the d4 of failure tells how bad.
      unsigned level=v.sides;bool first=true;int32_t sum=0;
      for(;;){unsigned f=retFaces(level),x=uniform(f);int pen=retPenalty(level);push(x,PartDie,v.negative,shapeOf(f));if(pen)push(-pen,PartNumber,true,TypeCount);
        sum+=int32_t(x)+pen;
        if(!v.negative&&first&&x==1){r.fail=uniform(4);failed+=sum;break;}
        first=false;if(v.negative||x!=f)break;r.successes++;if(level>=RetTop)break;level++;}
      r.total+=v.negative?-sum:sum;continue;}
    uint8_t shape=shapeOf(v.sides);
    for(unsigned j=0;j<v.count;j++){
      if(v.special==Sixty){push(uniform(6)*10+uniform(6),PartD66,v.negative,D66);r.special=true;continue;}
      unsigned x=uniform(v.sides);if(v.special==Percent){push(x,PartPercent,v.negative,DPercent);r.special=true;continue;}
      push(x,PartDie,v.negative,shape);r.total+=v.negative?-int32_t(x):int32_t(x);}
  }
  if(r.fail)r.total=failed; // a failure drops the bonuses
  r.serial=++rolls;resultCount++;events++;return true;
}
void Dicer::setPool(int count,int type,int mod){
  s.count=constrain(count,1,int(MaxCount));if(type>=0&&type<TypeCount)s.type=type;s.mod=constrain(mod,-MaxMod,MaxMod);changed();
}
String Dicer::resultText(const Result& r,bool sum) const{
  String v;for(unsigned i=0;i<r.parts;i++){if(i)v+=r.negative[i]?" - ":" + ";else if(r.negative[i])v+="-";
    v+=r.kind[i]==PartPercent?valueText(DPercent,r.value[i]):String(r.value[i]);}
  if(r.more)v+=" ...";if(sum&&!r.special)v+=" = "+String(r.total);return v;
}
void Dicer::gridChoose(int n){s.gridChosen=constrain(n,0,int(s.gridSize));gridRolled=false;changed();}
void Dicer::gridRoll(){
  if(!s.gridChosen)return;unsigned n=s.gridChosen,f=faces(s.gridType);
  for(unsigned i=0;i<n;i++)grid[i]=s.gridType==D66?uniform(6)*10+uniform(6):uniform(f);
  for(unsigned i=1;i<n;i++)for(unsigned j=i;j>0&&grid[j]>grid[j-1];j--)std::swap(grid[j],grid[j-1]); // high to low, as the app
  gridRolled=true;rolls++;events++;
}
void Dicer::gridAddRow(){if(s.gridSize+GridRow<=GridMax){for(unsigned i=s.gridSize;i<s.gridSize+GridRow;i++)grid[i]=1;s.gridSize+=GridRow;changed();}}
void Dicer::gridReset(){s.gridSize=GridStart;s.gridChosen=0;gridRolled=false;for(auto& v:grid)v=1;changed();}
void Dicer::gridSetType(int type){if(type<0||type>=TypeCount)return;s.gridType=type;gridRolled=false;s.threshold=constrain(s.threshold,2,int(faces(type)));changed();}
void Dicer::gridSetThreshold(bool on,int value){s.thresholdOn=on;s.threshold=constrain(value,2,int(faces(s.gridType)));changed();}
unsigned Dicer::gridSuccesses() const{if(!gridRolled||!s.thresholdOn)return 0;unsigned n=0;for(unsigned i=0;i<s.gridChosen;i++)n+=grid[i]>=s.threshold;return n;}

// ---- The sheet ----
int Dicer::find(const char* code) const{if(!code||!*code)return -1;for(unsigned i=0;i<sheet.count;i++)if(!strcasecmp(sheet.block[i].code,code))return i;return -1;}
int Dicer::valueAt(unsigned i,unsigned depth,bool& ok) const{
  const Block& b=sheet.block[i];if(b.kind==Stat&&b.formula[0]){Expr e{*this,b.formula,depth+1,false};long v=e.all();ok&=e.ok;return e.ok?clampInt(v,-StatLimit,StatLimit):0;}
  return b.value;
}
int Dicer::maxAt(unsigned i,unsigned depth,bool& ok) const{
  const Block& b=sheet.block[i];if(b.kind==Track&&b.formula[0]){Expr e{*this,b.formula,depth+1,false};long v=e.all();ok&=e.ok;return e.ok?clampInt(v,0,CounterLimit):0;}
  return b.kind==Track||b.kind==Pips?b.max:0;
}
int Dicer::numberAt(unsigned i,unsigned depth,bool& ok) const{const Block& b=sheet.block[i];int v=valueAt(i,depth,ok);return b.kind==Stat&&b.rule==Dnd?floorDiv(v-10,2):v;}
int Dicer::value(unsigned i) const{if(i>=sheet.count)return 0;bool ok=true;return valueAt(i,0,ok);}
int Dicer::maxOf(unsigned i) const{if(i>=sheet.count)return 0;bool ok=true;return maxAt(i,0,ok);}
bool Dicer::derivedOk(unsigned i) const{
  if(i>=sheet.count)return false;const Block& b=sheet.block[i];if(!b.formula[0]||(b.kind!=Stat&&b.kind!=Track))return true;
  bool ok=true;if(b.kind==Stat)valueAt(i,0,ok);else maxAt(i,0,ok);return ok;
}
int Dicer::number(unsigned i) const{if(i>=sheet.count)return 0;bool ok=true;return numberAt(i,0,ok);}
String Dicer::badge(unsigned i) const{
  if(i>=sheet.count)return String();const Block& b=sheet.block[i];if(b.kind!=Stat)return String();
  if(!derivedOk(i))return "?";
  if(b.rule==Dnd)return sign(number(i));if(b.rule==Ret)return retDie(constrain(value(i),0,int(RetTop)));return String();
}
// Puts the codes into a roll: a R.E.T. stat gives its die ("r4"), a D&D one its modifier, @code! the value.
bool Dicer::resolve(const char* f,String& out,bool strict) const{
  out="";const char* p=f;
  while(*p){
    if(*p!='@'){out+=char(tolower(*p));p++;continue;}
    p++;char c[CodeBytes+1];unsigned n=0;while(isalnum((unsigned char)*p)||*p=='_'){if(n>=CodeBytes)return false;c[n++]=tolower(*p);p++;}c[n]=0;bool raw=*p=='!';if(raw)p++;
    if(!n)return false;int i=find(c);
    if(i<0){if(strict)return false;out+="0";continue;}
    const Block& b=sheet.block[i];bool ok=true;
    if(b.kind==Stat&&b.rule==Ret&&!raw){out+="r"+String(constrain(valueAt(i,0,ok),0,int(RetTop)));if(!ok&&strict)return false;continue;}
    int v=raw?(b.kind==Stat?valueAt(i,0,ok):maxAt(i,0,ok)):(b.kind==Stat?numberAt(i,0,ok):valueAt(i,0,ok));if(!ok&&strict)return false;
    if(v<0){char last=out.length()?out[out.length()-1]:0;if(last=='+'||last=='-')out=out.substring(0,out.length()-1)+(last=='+'?'-':'+');else out+='-';out+=String(-v);}
    else out+=String(v);
  }
  out=compact(out);return true;
}
String Dicer::rollFormula(unsigned i) const{
  if(i>=sheet.count)return String();const Block& b=sheet.block[i];
  if(b.kind==Stat){if(!derivedOk(i))return String();
    if(b.rule==Dnd)return compact("d20"+sign(number(i)));if(b.rule==Bonus)return compact("d20"+sign(value(i)));if(b.rule==Ret)return "r"+String(constrain(value(i),0,int(RetTop)));return String();}
  if(b.kind!=Ability||!b.formula[0])return String();String v;return resolve(b.formula,v,true)&&valid(v)?v:String();
}
String Dicer::actName(unsigned i) const{
  if(i>=sheet.count)return String();const Block& b=sheet.block[i];
  if(b.kind==Pips)return t("Mark","Отметить");
  if(rollFormula(i).length())return t("Roll","Бросить");
  if(b.kind==Ability&&b.use[0]&&b.spend)return t("Use","Применить");
  return String();
}
String Dicer::noteText(unsigned i) const{if(i>=sheet.count)return String();const Block& b=sheet.block[i];String v;v.reserve(b.noteLen);for(unsigned k=0;k<b.noteLen;k++)v+=sheet.notes[b.note+k];return v;}
bool Dicer::act(unsigned i){
  why="";if(i>=sheet.count)return false;Block& b=sheet.block[i];
  if(b.kind==Pips){int m=maxOf(i);return setValue(i,b.value<m?b.value+1:0);}
  String f=rollFormula(i);int j=b.kind==Ability&&b.use[0]&&b.spend?find(b.use):-1;
  if(b.kind==Ability&&b.use[0]&&b.spend){
    if(j<0){why=t("No block with code ","Нет блока с кодом ")+b.use;return false;}
    if(value(j)<b.spend){why=t("Not enough: ","Не хватает: ")+sheet.block[j].name;return false;}}
  if(!f.length()&&j<0){why=b.kind==Ability&&b.formula[0]?t("The formula does not compute","Формула не считается"):String();return false;}
  if(f.length()&&!roll(f,b.name))return false;
  if(j>=0)change(j,-int(b.spend));
  return true;
}
bool Dicer::change(unsigned i,int delta){if(i>=sheet.count)return false;return setValue(i,sheet.block[i].value+delta);}
bool Dicer::setValue(unsigned i,int v){
  if(i>=sheet.count)return false;Block& b=sheet.block[i];
  switch(b.kind){
  case Stat:if(b.formula[0])return false;b.value=constrain(v,-StatLimit,StatLimit);break;
  case Track:b.value=constrain(v,-CounterLimit,CounterLimit);break;
  case Pips:b.value=constrain(v,0,int(b.max));break;
  default:return false;}
  sheetChanged(true);return true;
}
bool Dicer::setNote(SheetData& d,unsigned i,const String& v) const{
  if(v.length()>NoteBytes||!meshmesh::validUtf8((const uint8_t*)v.c_str(),v.length()))return false;
  if(d.notesUsed-d.block[i].noteLen+v.length()>NotesBytes)return false;
  char* tmp=new char[NotesBytes];unsigned at=0;
  for(unsigned k=0;k<d.count;k++){Block& b=d.block[k];const char* from=k==i?v.c_str():d.notes+b.note;unsigned len=k==i?v.length():b.noteLen;
    memcpy(tmp+at,from,len);b.note=at;b.noteLen=len;at+=len;}
  memcpy(d.notes,tmp,at);d.notesUsed=at;delete[] tmp;return true;
}
String Dicer::defaultName(uint8_t kind){
  switch(kind){case Stat:return t("Value","Значение");case Track:return t("Health","Здоровье");case Pips:return t("Marks","Отметки");
  case Ability:return t("Ability","Способность");case Note:return t("Notes","Заметки");default:return t("Section","Раздел");}
}
void Dicer::defaults(Block& b,uint8_t kind){
  b.kind=kind;b.value=b.max=0;b.rule=b.shape=0;
  switch(kind){case Stat:b.width=Quarter;b.value=10;break;case Track:b.width=Whole;b.value=b.max=10;break;case Pips:b.width=Third;b.max=3;break;
  case Ability:b.width=Half;break;default:b.width=Whole;break;}
  copyText(b.name,sizeof b.name,defaultName(kind).c_str());
}
int Dicer::addBlock(uint8_t kind,int at){
  if(sheet.count>=Blocks||kind>=KindCount)return -1;if(at<0||at>int(sheet.count))at=sheet.count;
  memmove(&sheet.block[at+1],&sheet.block[at],sizeof(Block)*(sheet.count-at));Block& b=sheet.block[at];memset(&b,0,sizeof b);
  b.note=sheet.notesUsed;b.color=sheet.count%Colors;sheet.count++;defaults(b,kind);
  setNote(sheet,at,String()); // the notes after it keep their order
  sheetChanged();return at;
}
bool Dicer::fresh(unsigned i) const{
  if(i>=sheet.count)return false;const Block& b=sheet.block[i];
  return !b.code[0]&&!b.formula[0]&&!b.use[0]&&!b.noteLen&&defaultName(b.kind)==b.name;
}
bool Dicer::renew(unsigned i,uint8_t kind){if(i>=sheet.count||kind>=KindCount)return false;defaults(sheet.block[i],kind);sheetChanged();return true;}
bool Dicer::setField(unsigned i,const String& field,const String& raw,String* error){
  auto bad=[&](const char* why){if(error)*error=why;return false;};
  if(i>=sheet.count)return bad("no such block");Block& b=sheet.block[i];String v=raw;
  auto number=[&](long& out){char* end;out=strtol(v.c_str(),&end,10);return v.length()&&*end==0;};long n=0;
  if(field=="note"){v.replace("\r","");v.replace("\\n","\n");if(!setNote(sheet,i,v))return bad("note: 200 bytes at most, 2048 on the sheet");}
  else{v.trim();
  if(field=="name"){if(!(v.length()?fits(v,BlockName)&&oneLine(v):b.kind==Note))return bad("name: 1-39 bytes");strlcpy(b.name,v.c_str(),sizeof b.name);}
  else if(field=="code"||field=="use"){v=lower(v);if(v.length()&&!codeOk(v))return bad("code: a Latin letter, then letters, digits, _; 7 at most");
    if(field=="code"){int j=find(v.c_str());if(v.length()&&j>=0&&j!=int(i))return bad("code: taken by another block");strlcpy(b.code,v.c_str(),sizeof b.code);}else strlcpy(b.use,v.c_str(),sizeof b.use);}
  else if(field=="formula"){v=clean(v);if(v.length()>BlockFormula||!oneLine(v))return bad("formula: 31 bytes at most");
    if(v.length()){if(b.kind==Ability){String r;if(!resolve(v.c_str(),r,false)||!valid(r))return bad("roll: d20+@dex+2, 2d6+@str, r3+@body!");}
      else{Expr e{*this,v.c_str(),0,true};e.all();if(!e.ok)return bad("value: 10+@dex, (@a+@b)/2, @body*2");}}
    strlcpy(b.formula,v.c_str(),sizeof b.formula);}
  else{if(!number(n))return bad("a number");
    if(field=="width"){if(n<0||n>=WidthCount)return bad("width 0-4");b.width=n;}
    else if(field=="shape"){if(n<0||n>=ShapeCount)return bad("shape 0-4");b.shape=n;}
    else if(field=="color"){if(n<0||n>=Colors)return bad("color 0-14");b.color=n;}
    else if(field=="rule"){if(n<0||n>=RuleCount)return bad("rule 0-3");b.rule=n;}
    else if(field=="kind"){if(n<0||n>=KindCount)return bad("kind 0-5");b.kind=n;if(b.kind==Pips)b.max=constrain(b.max,1,int(PipsMax)),b.value=constrain(b.value,0,int(b.max));}
    else if(field=="spend"){if(n<0||n>99)return bad("spend 0-99");b.spend=n;}
    else if(field=="max"){if(b.kind==Pips){if(n<1||n>PipsMax)return bad("pips 1-12");b.max=n;b.value=min<int>(b.value,n);}else{if(n<0||n>CounterLimit)return bad("max 0-9999");b.max=n;}}
    else if(field=="value"){b.value=b.kind==Pips?constrain(n,0L,long(b.max)):constrain(n,long(-CounterLimit),long(CounterLimit));}
    else return bad("field: name code width shape color rule kind value max formula use spend note");}}
  sheetChanged();return true;
}
bool Dicer::moveBlock(unsigned i,unsigned to){
  if(i>=sheet.count||to>=sheet.count)return false;if(i==to)return true;Block b=sheet.block[i];
  if(to>i)memmove(&sheet.block[i],&sheet.block[i+1],sizeof(Block)*(to-i));else memmove(&sheet.block[to+1],&sheet.block[to],sizeof(Block)*(i-to));
  sheet.block[to]=b;setNote(sheet,to,noteText(to));sheetChanged();return true; // the pool follows the new order
}
bool Dicer::deleteBlock(unsigned i){
  if(i>=sheet.count)return false;setNote(sheet,i,String());memmove(&sheet.block[i],&sheet.block[i+1],sizeof(Block)*(sheet.count-i-1));sheet.count--;sheetChanged();return true;
}
// Templates. The codes are Latin (formulas use them); the names follow the interface language.
bool Dicer::applyTemplate(uint8_t tmpl){
  if(tmpl>=TemplateCount)return false;blank(sheet);
  auto add=[&](uint8_t kind,uint8_t width,uint8_t shape,uint8_t rule,const char* code,const String& name,int value,int max,const char* formula,uint8_t color){
    if(sheet.count>=Blocks)return;Block& b=sheet.block[sheet.count++];memset(&b,0,sizeof b);b.kind=kind;b.width=width;b.shape=shape;b.rule=rule;b.value=value;b.max=max;b.color=color%Colors;b.note=sheet.notesUsed;
    strlcpy(b.code,code,sizeof b.code);copyText(b.name,sizeof b.name,name.c_str());strlcpy(b.formula,formula,sizeof b.formula);};
  auto note=[&](const String& v){setNote(sheet,sheet.count-1,v);};
  if(tmpl==Blank){add(Track,Third,Circle,Plain,"",t("Counter ","Счётчик ")+"1",CounterStart,0,"",0);}
  else if(tmpl==DndTemplate){
    const char* codes[6]={"str","dex","con","int","wis","cha"};
    String names[6]={t("STR","СИЛ"),t("DEX","ЛОВ"),t("CON","ТЕЛ"),t("INT","ИНТ"),t("WIS","МДР"),t("CHA","ХАР")};
    for(int k=0;k<6;k++)add(Stat,Sixth,Circle,Dnd,codes[k],names[k],10,0,"",k*2);
    add(Stat,Quarter,Square,Plain,"pb",t("Proficiency","Мастерство"),2,0,"",7);
    add(Stat,Quarter,Shield,Plain,"ac",t("Armor class","Класс доспеха"),10,0,"10+@dex",4);
    add(Stat,Quarter,Diamond,Bonus,"init",t("Initiative","Инициатива"),0,0,"@dex",9);
    add(Stat,Quarter,Hexagon,Plain,"spd",t("Speed","Скорость"),30,0,"",10);
    add(Track,Whole,Circle,Plain,"hp",t("Hit points","Хиты"),10,10,"",0);
    add(Pips,Third,Circle,Plain,"hd",t("Hit dice","Кости хитов"),1,1,"",2);
    add(Pips,Third,Circle,Plain,"dss",t("Death: saves","Смерть: успехи"),0,3,"",12);
    add(Pips,Third,Circle,Plain,"dsf",t("Death: fails","Смерть: провалы"),0,3,"",0);
    add(Header,Whole,Circle,Plain,"",t("Attacks","Атаки"),0,0,"",0);
    add(Ability,Half,Circle,Plain,"",t("Attack","Атака"),0,0,"d20+@str+@pb",0);note(t("Melee: STR + proficiency. Finesse or ranged: change @str to @dex.","Ближний бой: СИЛ + мастерство. Фехтовальное или дальнобойное: замените @str на @dex."));
    add(Ability,Half,Circle,Plain,"",t("Damage","Урон"),0,0,"1d8+@str",0);
    add(Header,Whole,Circle,Plain,"",t("Skills","Навыки"),0,0,"",0);
    // The rules' order; a proficient skill gets +@pb in its formula.
    const char* const skillAbility[18]={"dex","wis","int","str","cha","int","wis","cha","int","wis","int","wis","cha","int","dex","dex","cha","wis"};
    String skillName[18]={t("Acrobatics","Акробатика"),t("Animal Handling","Уход за животными"),t("Arcana","Магия"),t("Athletics","Атлетика"),t("Deception","Обман"),
      t("History","История"),t("Insight","Проницательность"),t("Intimidation","Запугивание"),t("Investigation","Анализ"),t("Medicine","Медицина"),t("Nature","Природа"),
      t("Perception","Восприятие"),t("Performance","Выступление"),t("Religion","Религия"),t("Sleight of Hand","Ловкость рук"),t("Stealth","Скрытность"),
      t("Persuasion","Убеждение"),t("Survival","Выживание")};
    uint8_t order[18];for(int k=0;k<18;k++)order[k]=k; // alphabetical in the interface language (UTF-8 byte order)
    for(int k=1;k<18;k++)for(int j=k;j>0&&strcmp(skillName[order[j]].c_str(),skillName[order[j-1]].c_str())<0;j--)std::swap(order[j],order[j-1]);
    for(int q=0;q<18;q++){int k=order[q];char f[24];snprintf(f,sizeof f,"d20+@%s",skillAbility[k]);add(Ability,Half,Circle,Plain,"",skillName[k],0,0,f,3);}
    add(Header,Whole,Circle,Plain,"",t("Magic","Магия"),0,0,"",0);
    add(Pips,Half,Circle,Plain,"sl1",t("Slots, level 1","Ячейки 1 круга"),2,2,"",9);
    add(Pips,Half,Circle,Plain,"insp",t("Inspiration","Вдохновение"),0,1,"",7);
    add(Note,Whole,Circle,Plain,"",t("Equipment","Снаряжение"),0,0,"",0);
  }else{
    // R.E.T.: three branches of the tree; each level I and II value is half the sum of the two below (rounded down).
    struct Branch{const char* code;const char* sub[2];const char* leaf[4];};
    const Branch tree[3]={{"mind",{"int","per"},{"edu","wit","att","intu"}},{"emo",{"emp","will"},{"insp","cha","comp","lead"}},{"body",{"str","agi"},{"phys","endu","fine","ctrl"}}};
    String top[3]={t("Mind","Разум"),t("Emotions","Эмоции"),t("Body","Тело")};
    String mid[6]={t("Intellect","Интеллект"),t("Perception","Восприятие"),t("Empathy","Эмпатия"),t("Will","Воля"),t("Strength","Сила"),t("Agility","Ловкость")};
    String low[12]={t("Education","Образование"),t("Wits","Сообразительность"),t("Attention","Внимание"),t("Intuition","Интуиция"),t("Inspiration","Вдохновение"),t("Charisma","Харизма"),
      t("Composure","Самообладание"),t("Leadership","Лидерство"),t("Physique","Телосложение"),t("Endurance","Выносливость"),t("Fine motor","Мелкая моторика"),t("Body control","Контроль тела")};
    const uint8_t hue[3]={4,5,0};
    for(int k=0;k<3;k++){const Branch& br=tree[k];char f[32];
      snprintf(f,sizeof f,"(@%s+@%s)/2",br.sub[0],br.sub[1]);add(Stat,Third,Hexagon,Ret,br.code,top[k],3,0,f,hue[k]);
      for(int j=0;j<2;j++){snprintf(f,sizeof f,"(@%s+@%s)/2",br.leaf[j*2],br.leaf[j*2+1]);add(Stat,Third,Square,Ret,br.sub[j],mid[k*2+j],3,0,f,hue[k]);}
      for(int j=0;j<4;j++)add(Stat,Quarter,Circle,Ret,br.leaf[j],low[k*4+j],3,0,"",hue[k]);}
    add(Track,Third,Circle,Plain,"hp",t("Health","Здоровье"),6,0,"@body*2",0);
    add(Track,Third,Circle,Plain,"san",t("Sanity","Рассудок"),6,0,"@mind+@emo",9);
    add(Track,Third,Circle,Plain,"luck",t("Luck","Удача"),6,0,"@lvl*2",7);note(t("Declared before a roll and added to it; a failure burns it. A failure gives 1 luck, a critical one 3.","Заявляется до броска и прибавляется к нему, при провале сгорает. Провал даёт 1 удачи, критический — 3."));
    add(Stat,Quarter,Diamond,Plain,"lvl",t("Level","Уровень"),3,0,"(@mind+@emo+@body)/3",10);
    add(Stat,Quarter,Shield,Plain,"hit",t("Strike","Сила удара"),1,0,"@str/2",0);
    add(Stat,Quarter,Hexagon,Plain,"spd",t("Speed","Скорость"),3,0,"",3);
    add(Stat,Quarter,Square,Plain,"dev",t("Growth points","Очки развития"),0,0,"",12);
    add(Header,Whole,Circle,Plain,"",t("Skills and talents","Навыки и таланты"),0,0,"",0);
    add(Ability,Half,Circle,Plain,"",t("Skill: Persuasion","Навык: Убеждение"),0,0,"@cha+3",5);note(t("A skill: the die of its stat plus the skill's level (3 at the start, 7 at most).","Навык: куб параметра плюс уровень навыка (на старте 3, не больше 7)."));
    add(Ability,Half,Circle,Plain,"",t("Talent: Shooting","Талант: Стрельба"),0,0,"r3+@fine!",0);note(t("A talent: the die of its own level plus the level of its stat.","Талант: куб своего уровня плюс уровень параметра-основы."));
    add(Ability,Half,Circle,Plain,"",t("Dodge","Уклонение"),0,0,"@agi",0);
    add(Ability,Half,Circle,Plain,"",t("Initiative","Инициатива"),0,0,"@per",4);
    add(Note,Whole,Circle,Plain,"",t("Rolls","Броски"),0,0,"",0);
    note(t("The top value of a die is a success: roll the die of the next level and add it, up to d100. A 1 is a failure: d4 of failure, 1 is critical; bonuses are lost.","Максимум — успех: + куб следующего уровня (до d100). Единица — провал: d4, на 1 — критический, бонусы сгорают."));
    add(Note,Whole,Circle,Plain,"",t("Equipment","Снаряжение"),0,0,"",0);
  }
  sheetChanged();return true;
}
String Dicer::exportText() const{
  String who=heroName(s.character),v="MMS1~N|"+escape(who.c_str(),NameBytes);
  for(unsigned i=0;i<sheet.count;i++){const Block& b=sheet.block[i];String note=noteText(i);
    String f[13]={num(b.kind),num(b.width),num(b.shape),num(b.color),num(b.rule),escape(b.code,CodeBytes),escape(b.name,BlockName),num(b.value),num(b.max),
      escape(b.formula,BlockFormula),escape(b.use,CodeBytes),num(b.spend),escape(note.c_str(),note.length())};
    int last=12;while(last>0&&!f[last].length())last--;v+='~';for(int k=0;k<=last;k++){if(k)v+='|';v+=f[k];}}
  return v;
}
bool Dicer::parseSheet(const String& text,SheetData& d,String& name,String* error) const{
  auto bad=[&](const char* why){if(error)*error=why;return false;};
  if(!text.startsWith("MMS1~"))return bad("not a MeshMesh sheet (MMS1)");
  blank(d);name="";unsigned records=split(text,'~',nullptr,0);if(records>Blocks+2)return bad("64 blocks at most");
  String* rec=new String[records];split(text,'~',rec,records);bool ok=true;
  for(unsigned r=1;r<records&&ok;r++){
    String f[14];unsigned n=split(rec[r],'|',f,14);
    if(f[0]=="N"){name=unescape(f[1]);if(!fits(name,NameBytes))ok=bad("name: 1-23 bytes");continue;}
    if(n>13||d.count>=Blocks){ok=bad("a block has too many fields");break;}
    Block& b=d.block[d.count];memset(&b,0,sizeof b);long v[13]={};
    for(int k:{0,1,2,3,4,7,8,11}){char* end;v[k]=strtol(f[k].c_str(),&end,10);if(*end){ok=bad("a number expected");break;}}
    if(!ok)break;
    String code=lower(unescape(f[5])),bname=unescape(f[6]),formula=unescape(f[9]),use=lower(unescape(f[10])),note=unescape(f[12]);
    b.kind=v[0];b.width=v[1];b.shape=v[2];b.color=v[3];b.rule=v[4];b.spend=constrain(v[11],0L,99L);b.note=d.notesUsed;
    if(v[0]<0||v[0]>=KindCount||v[1]<0||v[1]>=WidthCount||v[2]<0||v[2]>=ShapeCount||v[3]<0||v[3]>=Colors||v[4]<0||v[4]>=RuleCount){ok=bad("a value out of range");break;}
    if((code.length()&&!codeOk(code))||(use.length()&&!codeOk(use))||bname.length()>BlockName||!oneLine(bname)||formula.length()>BlockFormula||!oneLine(formula)
      ||!meshmesh::validUtf8((const uint8_t*)bname.c_str(),bname.length())){ok=bad("a block's code, name or formula is invalid");break;}
    b.value=constrain(v[7],long(-CounterLimit),long(CounterLimit));b.max=constrain(v[8],0L,long(CounterLimit));if(b.kind==Pips){b.max=constrain(b.max,1,int(PipsMax));b.value=constrain(b.value,0,int(b.max));}
    strlcpy(b.code,code.c_str(),sizeof b.code);strlcpy(b.use,use.c_str(),sizeof b.use);strlcpy(b.name,bname.c_str(),sizeof b.name);strlcpy(b.formula,formula.c_str(),sizeof b.formula);
    d.count++;if(!setNote(d,d.count-1,note)){ok=bad("notes: 200 bytes each, 2048 on the sheet");break;}
  }
  delete[] rec;if(ok&&!name.length())name=t("Character","Персонаж");return ok;
}
bool Dicer::importText(const String& text,String* error){
  SheetData* d=new SheetData;String name;bool ok=parseSheet(text,*d,name,error);
  if(ok&&s.characters>=Characters){ok=false;if(error)*error="6 characters at most";}
  if(ok){addCharacter(name);sheet.count=d->count;sheet.notesUsed=d->notesUsed;memcpy(sheet.block,d->block,sizeof(Block)*d->count);memcpy(sheet.notes,d->notes,d->notesUsed);sheetChanged();}
  delete d;return ok;
}
String Dicer::sheetJson() const{
  String v;v.reserve(512+sheet.count*96+sheet.notesUsed);
  v+="{\"rev\":"+String(sheetRev)+",\"character\":"+String(s.character)+",\"name\":"+jsonText(heroName(s.character).c_str())+",\"notes_free\":"+String(NotesBytes-sheet.notesUsed)+",\"blocks\":[";
  for(unsigned i=0;i<sheet.count;i++){const Block& b=sheet.block[i];if(i)v+=',';
    v+="{\"k\":"+String(b.kind)+",\"w\":"+String(b.width)+",\"c\":"+String(b.color)+",\"name\":"+jsonText(b.name);
    if(b.shape)v+=",\"s\":"+String(b.shape);if(b.rule)v+=",\"r\":"+String(b.rule);if(b.code[0])v+=",\"code\":"+jsonText(b.code);
    if(b.kind==Stat||b.kind==Track||b.kind==Pips)v+=",\"v\":"+String(value(i));
    if(b.kind==Track||b.kind==Pips)v+=",\"x\":"+String(maxOf(i));
    if(b.kind==Stat||b.kind==Track){if(b.formula[0])v+=",\"f\":"+jsonText(b.formula)+(derivedOk(i)?"":",\"bad\":true");if(b.kind==Track&&b.max)v+=",\"m\":"+String(b.max);if(b.kind==Stat&&b.formula[0])v+=",\"raw\":"+String(b.value);}
    if(b.kind==Ability&&b.formula[0])v+=",\"f\":"+jsonText(b.formula);
    if(b.use[0])v+=",\"use\":"+jsonText(b.use)+",\"sp\":"+String(b.spend);
    if(b.noteLen){String n=noteText(i);v+=",\"note\":"+jsonText(n.c_str());}
    String bd=badge(i);if(bd.length())v+=",\"b\":"+jsonText(bd.c_str());
    String rf=rollFormula(i);if(rf.length())v+=",\"roll\":"+jsonText(rf.c_str());else if(b.kind==Ability&&b.formula[0])v+=",\"bad\":true";
    v+='}';
  }
  return v+"]}";
}
// Saved rolls: the abilities with a formula.
unsigned Dicer::savedCount() const{unsigned n=0;for(unsigned i=0;i<sheet.count;i++)n+=sheet.block[i].kind==Ability&&sheet.block[i].formula[0];return n;}
int Dicer::savedBlock(unsigned k) const{for(unsigned i=0;i<sheet.count;i++)if(sheet.block[i].kind==Ability&&sheet.block[i].formula[0]&&!k--)return i;return -1;}
bool Dicer::rollSaved(unsigned k){int i=savedBlock(k);return i>=0&&act(i);}
bool Dicer::addRoll(const String& formula,const String& name){
  String f=clean(formula),v=name;v.trim();if(sheet.count>=Blocks||!fits(v,BlockName))return false;String r;if(!resolve(f.c_str(),r,false)||!valid(r))return false;
  int i=addBlock(Ability);if(i<0)return false;Block& b=sheet.block[i];strlcpy(b.formula,f.c_str(),sizeof b.formula);strlcpy(b.name,v.c_str(),sizeof b.name);sheetChanged();return true;
}
bool Dicer::editRoll(unsigned k,const String& formula,const String& name){int i=savedBlock(k);return i>=0&&setField(i,"formula",formula)&&setField(i,"name",name);}
bool Dicer::deleteRoll(unsigned k){int i=savedBlock(k);return i>=0&&deleteBlock(i);}
bool Dicer::colorRoll(unsigned k,unsigned color){int i=savedBlock(k);if(i<0||color>=Colors)return false;sheet.block[i].color=color;sheetChanged();return true;}

// ---- Characters and storage ----
bool Dicer::loadSheet(uint8_t file){
  blank(sheet);if(!fsOk())return false;
  for(bool temp:{false,true}){String path=sheetPath(file,temp);if(!LittleFS.exists(path.c_str()))continue;File f=LittleFS.open(path.c_str(),FILE_READ);if(!f)continue;
    SheetHead h;bool ok=f.read((uint8_t*)&h,sizeof h)==sizeof h&&h.magic==SheetMagic&&h.count<=Blocks&&h.notes<=NotesBytes&&f.size()==sizeof h+sizeof(Block)*h.count+h.notes;
    if(ok)ok=f.read((uint8_t*)sheet.block,sizeof(Block)*h.count)==sizeof(Block)*h.count&&f.read((uint8_t*)sheet.notes,h.notes)==h.notes;
    f.close();sheet.count=h.count;sheet.notesUsed=h.notes;
    for(unsigned i=0;ok&&i<sheet.count;i++)ok=blockOk(sheet.block[i],sheet.notesUsed);
    if(ok&&sheetCheck(sheet)==h.check)return true;blank(sheet);}
  return false;
}
void Dicer::saveSheet(){
  if(!fsOk())return;uint8_t file=hero().file;SheetHead h{SheetMagic,sheet.count,sheet.notesUsed,sheetCheck(sheet)};
  String path=sheetPath(file,false),temp=sheetPath(file,true);LittleFS.mkdir("/meshmesh");if(LittleFS.exists(temp.c_str()))LittleFS.remove(temp.c_str());
  File f=LittleFS.open(temp.c_str(),FILE_WRITE);if(!f)return;
  bool ok=f.write((const uint8_t*)&h,sizeof h)==sizeof h&&f.write((const uint8_t*)sheet.block,sizeof(Block)*sheet.count)==sizeof(Block)*sheet.count&&f.write((const uint8_t*)sheet.notes,sheet.notesUsed)==sheet.notesUsed;
  f.close();if(!ok){LittleFS.remove(temp.c_str());return;}
#if defined(MM_NRF52)
  if(LittleFS.exists(path.c_str()))LittleFS.remove(path.c_str()); // littlefs v1 of the Adafruit core does not rename over a file
#endif
  LittleFS.rename(temp.c_str(),path.c_str());
}
bool Dicer::addCharacter(const String& name,uint8_t tmpl){
  String v=name;v.trim();if(s.characters>=Characters||!fits(v,NameBytes)||tmpl>=TemplateCount)return false;
  uint8_t file=0;for(;file<SheetFiles;file++){bool used=false;for(unsigned i=0;i<s.characters;i++)used|=s.list[i].file==file;if(!used)break;}
  if(saveDue)save();Character& c=s.list[s.characters];memset(&c,0,sizeof c);strlcpy(c.name,v.c_str(),sizeof c.name);c.file=file;
  s.character=s.characters++;blank(sheet);applyTemplate(tmpl);return true;
}
bool Dicer::useCharacter(unsigned i){if(i>=s.characters)return false;if(i==s.character)return true;if(saveDue)save();s.character=i;loadSheet(hero().file);sheetChanged();return true;}
bool Dicer::renameCharacter(const String& name){String v=name;v.trim();if(!fits(v,NameBytes))return false;strlcpy(hero().name,v.c_str(),sizeof hero().name);changed();return true;}
bool Dicer::deleteCharacter(){
  if(!s.character)return false;unsigned i=s.character;
  if(fsOk()){LittleFS.remove(sheetPath(hero().file,false).c_str());LittleFS.remove(sheetPath(hero().file,true).c_str());}
  memmove(&s.list[i],&s.list[i+1],sizeof(Character)*(s.characters-i-1));
  s.characters--;memset(&s.list[s.characters],0,sizeof(Character));s.character=0;saveDue=0;loadSheet(hero().file);sheetChanged();return true;
}
void Dicer::setMode(uint8_t mode){if(mode<ModeCount&&mode!=s.mode){s.mode=mode;changed();}}
bool Dicer::migrate(const uint8_t* stored,size_t size){
  if(size!=sizeof(V1State))return false;const V1State& o=*(const V1State*)stored;
  bool ok=o.version==1&&o.check==fnv(stored,offsetof(V1State,check))&&o.characters>=1&&o.characters<=Characters&&o.character<o.characters&&o.mode<ModeCount&&o.type<TypeCount&&o.gridType<TypeCount
    &&o.count>=1&&o.count<=MaxCount&&o.gridSize>=GridStart&&o.gridSize<=GridMax&&o.gridChosen<=o.gridSize;
  for(unsigned i=0;ok&&i<o.characters;i++){const V1Character& c=o.list[i];ok=c.rolls<=10&&c.counters<=6&&memchr(c.name,0,sizeof c.name);
    for(unsigned j=0;ok&&j<c.rolls;j++)ok=memchr(c.roll[j].name,0,sizeof c.roll[j].name)&&memchr(c.roll[j].formula,0,sizeof c.roll[j].formula)&&c.roll[j].color<Colors;
    for(unsigned j=0;ok&&j<c.counters;j++)ok=memchr(c.counter[j].name,0,sizeof c.counter[j].name)&&c.counter[j].color<Colors;}
  if(!ok)return false;
  s=State();s.character=0;s.characters=o.characters;s.mode=o.mode;s.type=o.type;s.count=o.count;s.mod=o.mod;
  s.gridType=o.gridType;s.gridSize=o.gridSize;s.gridChosen=o.gridChosen;s.threshold=o.threshold;s.thresholdOn=o.thresholdOn;
  // Each character: its counters become tracks without a maximum, its saved rolls abilities.
  for(unsigned i=0;i<o.characters;i++){const V1Character& c=o.list[i];strlcpy(s.list[i].name,c.name,sizeof s.list[i].name);s.list[i].file=i;s.character=i;blank(sheet);
    for(unsigned j=0;j<c.counters;j++){Block& b=sheet.block[sheet.count++];memset(&b,0,sizeof b);b.kind=Track;b.width=Third;b.value=c.counter[j].value;b.color=c.counter[j].color;strlcpy(b.name,c.counter[j].name,sizeof b.name);}
    for(unsigned j=0;j<c.rolls;j++){Block& b=sheet.block[sheet.count++];memset(&b,0,sizeof b);b.kind=Ability;b.width=Half;b.color=c.roll[j].color;strlcpy(b.name,c.roll[j].name,sizeof b.name);strlcpy(b.formula,c.roll[j].formula,sizeof b.formula);}
    saveSheet();}
  s.character=o.character;loadSheet(hero().file);save();return true;
}
void Dicer::begin(){
  State* stored=new State;
  bool ok=readStored(DiceFile,DiceTemp,stored,sizeof(State))&&stored->version==3&&stored->check==fnv((const uint8_t*)stored,offsetof(State,check));
  if(!ok){V2State* v=new V2State;if(readStored(DiceFile,DiceTemp,v,sizeof(V2State))&&v->version==2&&v->check==fnv((const uint8_t*)v,offsetof(V2State,check))){
    *stored=State();stored->character=v->character;stored->characters=v->characters;stored->mode=v->mode;stored->type=v->type;stored->count=v->count;stored->mod=v->mod;
    stored->gridType=v->gridType;stored->gridSize=v->gridSize;stored->gridChosen=v->gridChosen;stored->threshold=v->threshold;stored->thresholdOn=v->thresholdOn;
    memcpy(stored->list,v->list,sizeof stored->list);ok=true;}delete v;}
  ok=ok
    &&stored->characters>=1&&stored->characters<=Characters&&stored->character<stored->characters&&stored->mode<ModeCount&&stored->type<TypeCount&&stored->gridType<TypeCount
    &&stored->count>=1&&stored->count<=MaxCount&&stored->gridSize>=GridStart&&stored->gridSize<=GridMax&&stored->gridChosen<=stored->gridSize;
  for(unsigned i=0;ok&&i<stored->characters;i++)ok=memchr(stored->list[i].name,0,sizeof stored->list[i].name)&&stored->list[i].file<SheetFiles;
  if(ok)s=*stored;
  delete stored;
  if(!ok){V1State* old=new V1State;ok=readStored(DiceFile,DiceTemp,old,sizeof(V1State))&&migrate((const uint8_t*)old,sizeof(V1State));delete old;}
  if(!ok)s=State();
  loadSheet(hero().file);
  if(!(s.flags&ExamplesAdded))addExamples();
  for(auto& v:grid)v=1;events++;sheetRev++;
}
// The two systems as characters of their own: open, look, change or delete them; added once, where there is room.
void Dicer::addExamples(){
  s.flags|=ExamplesAdded;uint8_t keep=s.character;
  if(s.characters<Characters)addCharacter("D&D 5e",DndTemplate);
  if(s.characters<Characters)addCharacter(t("R.E.T.","Р.Э.Т."),RetTemplate);
  useCharacter(keep);save();
}
void Dicer::changed(bool soon){events++;if(soon){saveDue=millis()+2000;return;}save();}
void Dicer::save(){saveDue=0;s.check=fnv((const uint8_t*)&s,offsetof(State,check));writeStored(DiceFile,DiceTemp,&s,sizeof(State));saveSheet();}

// ---- Sending a sheet ----
// Parts: "🎲S 3F2A 2/9 text", each an acknowledged direct message; the next goes after the ACK of the last,
// a part is tried three times. The receiver keeps the parts ten minutes and offers the whole sheet.
namespace {
String chunkOf(const String& text,unsigned k){ // the k-th piece of at most Chunk bytes, cut between UTF-8 characters
  unsigned at=0;for(unsigned part=0;;part++){unsigned end=min<unsigned>(text.length(),at+Chunk);while(end<text.length()&&(uint8_t(text[end])&0xc0)==0x80)end--;
    while(end<text.length()&&end>at+1&&text[end-1]==' ')end--; // a piece does not end with a space (a line may lose it)
    if(part==k)return text.substring(at,end);at=end;if(at>=text.length())return String();}
}
unsigned chunks(const String& text){unsigned n=0;while(chunkOf(text,n).length())n++;return n;}
const char* peerName(uint64_t id){const Peer* p=meshRadio.findContact(id);return p?p->name:"";}
}
bool Dicer::send(uint64_t peer,String* error){
  auto bad=[&](const String& why){if(error)*error=why;return false;};
  if(out.parts&&!out.done&&!out.failed)return bad("a sheet is being sent");
  const Peer* p=meshRadio.findContact(peer);if(!p||p->type!=1)return bad("not a chat contact");
  String text=exportText();unsigned n=chunks(text);if(!n||n>MaxParts)return bad("the sheet is too large");
  out=Outgoing();out.peer=peer;strlcpy(out.name,p->name,sizeof out.name);out.id=uniform(0xffff);out.parts=n;out.text=text;
  note=t("Sending the sheet to ","Отправка листа: ")+out.name;notes++;events++;sendNext();return true;
}
void Dicer::sendNext(){
  if(!out.parts||out.done||out.failed)return;char head[24];snprintf(head,sizeof head,"S %04X %u/%u ",out.id,out.part+1,out.parts);
  out.at=millis();out.msg=meshRadio.sendGame(String(Tag)+head+chunkOf(out.text,out.part),out.peer); // 0: the radio is busy, tick() tries again
}
bool Dicer::delivery(uint32_t id,uint8_t status){
  if(!out.msg||out.msg!=id||out.done||out.failed)return false;
  if(status==ChatMessage::Delivered){out.msg=0;out.tries=0;out.part++;events++;
    if(out.part>=out.parts){out.done=true;out.text="";note=t("Sheet delivered to ","Лист доставлен: ")+out.name;notes++;}else sendNext();}
  else if(status==ChatMessage::Failed){out.msg=0;if(++out.tries>=3){out.failed=true;out.text="";note=t("Sheet not delivered to ","Лист не доставлен: ")+out.name;notes++;events++;}else sendNext();}
  return true;
}
bool Dicer::receive(uint64_t from,const char* name,const char* text){
  if(strncmp(text,Tag,4)||text[4]!='S')return false;
  unsigned id=0,k=0,n=0;int at=0; // exactly one space before the piece: a piece may start with a space
  if(sscanf(text+5," %4x %u/%u%n",&id,&k,&n,&at)<3||!at||text[5+at]!=' '||!k||k>n||n>MaxParts)return true;
  const char* chunk=text+5+at+1;size_t len=strlen(chunk);if(len>Chunk)return true;
  if(in&&(in->peer!=from||in->id!=id||in->parts!=n)){free(in->buf);delete in;in=nullptr;}
  if(!in){in=new Incoming();in->buf=(char*)malloc(n*Chunk);if(!in->buf){delete in;in=nullptr;return true;}in->peer=from;in->id=id;in->parts=n;strlcpy(in->from,name?name:"",sizeof in->from);}
  if(len){memcpy(in->buf+(k-1)*Chunk,chunk,len);in->len[k-1]=len;}in->at=millis();
  return true;
}
bool Dicer::acceptOffer(){if(!offered)return false;String error;if(!importText(offer.text,&error)){why=error;return false;}offered=false;offer.text="";events++;return true;}
void Dicer::declineOffer(){offered=false;offer.text="";events++;}
void Dicer::tick(){
  uint32_t now=millis();if(saveDue&&int32_t(now-saveDue)>=0)save();
  if(out.parts&&!out.done&&!out.failed&&((!out.msg&&now-out.at>3000)||(out.msg&&now-out.at>120000))){if(out.msg&&++out.tries>=3){out.failed=true;out.msg=0;out.text="";note=t("Sheet not delivered to ","Лист не доставлен: ")+out.name;notes++;events++;}else sendNext();}
  if(in){bool all=true;for(unsigned k=0;k<in->parts;k++)all&=in->len[k]>0;
    if(all){String text;text.reserve(in->parts*Chunk);for(unsigned k=0;k<in->parts;k++)for(unsigned j=0;j<in->len[k];j++)text+=in->buf[k*Chunk+j];
      SheetData* d=new SheetData;String name;bool ok=parseSheet(text,*d,name,nullptr);delete d;
      if(ok){offer=Offer();offer.peer=in->peer;strlcpy(offer.from,in->from[0]?in->from:peerName(in->peer),sizeof offer.from);strlcpy(offer.name,name.c_str(),sizeof offer.name);offer.text=text;offered=true;
        note=t("Character sheet from ","Лист персонажа от ")+offer.from+": "+offer.name;notes++;events++;}
      free(in->buf);delete in;in=nullptr;}
    else if(now-in->at>600000){free(in->buf);delete in;in=nullptr;}}
}
String Dicer::json() const{
  DynamicJsonDocument d(8192);const char* modes[]={"rpg","grid","sheet"};d["mode"]=modes[s.mode<ModeCount?s.mode:0];
  d["character"]=s.character;JsonArray hs=d.createNestedArray("characters");for(unsigned i=0;i<s.characters;i++)hs.add(heroName(i));
  JsonObject p=d.createNestedObject("pool");p["count"]=s.count;p["type"]=typeNames[s.type];p["mod"]=s.mod;p["text"]=poolText();
  JsonArray rs=d.createNestedArray("rolls");for(unsigned k=0;k<savedCount();k++){int i=savedBlock(k);const Block& b=sheet.block[i];JsonObject o=rs.createNestedObject();o["name"]=b.name;o["formula"]=b.formula;String r=rollFormula(i);if(r!=b.formula)o["roll"]=r;o["color"]=b.color;o["block"]=i;}
  // Parts: value*4+kind (kind 0 die, 1 number, 2 d66, 3 d%), negative when subtracted; one number each keeps the reply small.
  JsonArray out=d.createNestedArray("results");
  for(unsigned i=0;i<resultCount;i++){const Result& r=results[i];JsonObject o=out.createNestedObject();o["n"]=r.serial;if(r.label[0])o["label"]=r.label;o["formula"]=r.formula;
    JsonArray ps=o.createNestedArray("parts");for(unsigned k=0;k<r.parts;k++){long v=long(r.value[k])*4+r.kind[k];ps.add(r.negative[k]?-v:v);}
    if(r.more)o["more"]=true;if(r.special)o["special"]=true;else o["total"]=r.total;if(r.successes)o["successes"]=r.successes;if(r.fail)o["fail"]=r.fail;
    if(strchr(r.formula,'r')){JsonArray ds=o.createNestedArray("dice");for(unsigned k=0;k<r.parts;k++)ds.add(r.kind[k]==PartNumber?"n":r.shape[k]<TypeCount?typeNames[r.shape[k]]:"");}} // R.E.T.: the dice of each level
  JsonObject g=d.createNestedObject("grid");g["type"]=typeNames[s.gridType];g["size"]=s.gridSize;g["chosen"]=s.gridChosen;g["rolled"]=gridRolled;
  g["threshold"]=s.threshold;g["threshold_on"]=s.thresholdOn;g["successes"]=gridSuccesses();
  JsonArray gv=g.createNestedArray("values");for(unsigned i=0;i<s.gridSize;i++)gv.add(grid[i]);
  JsonObject sh=d.createNestedObject("sheet");sh["rev"]=sheetRev;sh["blocks"]=sheet.count;
  if(offered){JsonObject o=d.createNestedObject("offer");o["from"]=offer.from;o["name"]=offer.name;o["peer"]=meshRadio.idText(offer.peer);}
  if(this->out.parts){JsonObject o=d.createNestedObject("send");o["to"]=this->out.name;o["part"]=this->out.part;o["parts"]=this->out.parts;o["state"]=this->out.done?"done":this->out.failed?"failed":"sending";}
  if(notes){d["note"]=note;d["notes"]=notes;}
  d["rolls_total"]=rolls;
  if(d.overflowed())d["overflowed"]=true;
  String v;serializeJson(d,v);return v;
}
// USB, BLE and the web page (docs/dice.md): "dice" shows the state; "dice roll [FORMULA]", "dice pool COUNT TYPE MOD",
// "dice mode rpg|grid|sheet", "dice clear"; "dice saved add|edit I|del I|color I C|roll I"; "dice grid choose N|roll|row|reset|type T|threshold off|N";
// "dice sheet [add KIND [AT]|set I FIELD VALUE|move I TO|del I|act I|I +N|I -N|I set N|template T|export|import TEXT|put K/N TEXT|send NODE|offer accept|decline]";
// "dice char add NAME|new TEMPLATE NAME|use I|name NAME|del". A saved roll's formula comes first, its name after it: "dice saved add 8d6 Fireball".
String Dicer::command(const String& line){
  String arg=line.length()>5?line.substring(5):String();arg.trim();
  auto ok=[&](const String& r){return "OK "+r+" "+json();};
  auto take=[](String& rest){int sp=rest.indexOf(' ');String w=sp<0?rest:rest.substring(0,sp);rest=sp<0?String():rest.substring(sp+1);rest.trim();return w;};
  auto number=[](const String& w,long& v){if(!w.length())return false;char* end;v=strtol(w.c_str(),&end,10);return *end==0;};
  auto type=[](const String& w){for(int i=0;i<TypeCount;i++)if(w==typeNames[i])return i;return -1;};
  if(!arg.length())return json();
  String rest=arg,op=take(rest);long a=0,b=0;
  if(op=="roll"){if(!rest.length()){rollPool();return ok("rolled");}return roll(rest)?ok("rolled"):"ERR formula: NdX+NdX-N, X 2..1000, d% d66, rN (R.E.T. level 0-8), up to 8 terms";}
  if(op=="clear"){clearResults();return ok("cleared");}
  if(op=="mode"){const char* modes[]={"rpg","grid","sheet"};if(rest=="counters")rest="sheet";for(int i=0;i<ModeCount;i++)if(rest==modes[i]){setMode(i);return ok("mode");}return "ERR mode rpg|grid|sheet";}
  if(op=="pool"){String c=take(rest),k=take(rest),m=take(rest);int ty=type(k);if(!number(c,a)||ty<0||(m.length()&&!number(m,b)))return "ERR pool COUNT TYPE [MOD]";setPool(a,ty,b);return ok("pool");}
  if(op=="saved"){String w=take(rest);
    if(w=="add"){String f=take(rest);return addRoll(f,rest)?ok("added"):"ERR saved add FORMULA NAME (64 blocks, name 1-23 bytes)";}
    if(!number(take(rest),a))return "ERR saved add|edit I|del I|color I C|roll I";
    if(w=="roll")return rollSaved(a)?ok("rolled"):"ERR "+(why.length()?why:String("no such roll"));
    if(w=="del")return deleteRoll(a)?ok("deleted"):"ERR no such roll";
    if(w=="color")return number(rest,b)&&colorRoll(a,b)?ok("color"):"ERR saved color I 0-14";
    if(w=="edit"){String f=take(rest);return editRoll(a,f,rest)?ok("saved"):"ERR saved edit I FORMULA NAME";}
    return "ERR saved add|edit|del|color|roll";}
  if(op=="grid"){String w=take(rest);
    if(w=="choose")return number(rest,a)?(gridChoose(a),ok("chosen")):"ERR grid choose N";
    if(w=="roll"){if(!s.gridChosen)return "ERR choose the dice first";gridRoll();return ok("rolled");}
    if(w=="row"){if(s.gridSize>=GridMax)return "ERR 50 dice at most";gridAddRow();return ok("row");}
    if(w=="reset"){gridReset();return ok("reset");}
    if(w=="type"){int ty=type(rest);if(ty<0)return "ERR grid type d4|d6|d8|d10|d12|d20|d66|d%";gridSetType(ty);return ok("type");}
    if(w=="threshold"){if(rest=="off"){gridSetThreshold(false,s.threshold);return ok("threshold");}if(!number(rest,a))return "ERR grid threshold off|N";gridSetThreshold(true,a);return ok("threshold");}
    return "ERR grid choose|roll|row|reset|type|threshold";}
  if(op=="sheet"){String w=take(rest);
    if(!w.length())return sheetJson();
    if(w=="add"){String k=take(rest);int kind=-1;for(int i=0;i<KindCount;i++)if(k==kindNames[i])kind=i;if(kind<0)return "ERR sheet add stat|track|pips|ability|note|header [AT]";
      int at=-1;if(rest.length()){if(!number(rest,a))return "ERR sheet add KIND [AT]";at=a;}int i=addBlock(kind,at);return i<0?"ERR 64 blocks at most":ok("added "+String(i));}
    if(w=="set"){if(!number(take(rest),a))return "ERR sheet set I FIELD VALUE";String field=take(rest),error;return setField(a,field,rest,&error)?ok("set"):"ERR "+error;}
    if(w=="move"){if(!number(take(rest),a)||!number(rest,b))return "ERR sheet move I TO";return moveBlock(a,b)?ok("moved"):"ERR no such block";}
    if(w=="del"){if(!number(rest,a))return "ERR sheet del I";return deleteBlock(a)?ok("deleted"):"ERR no such block";}
    if(w=="act"){if(!number(rest,a))return "ERR sheet act I";return act(a)?ok("done"):"ERR "+(why.length()?why:String("nothing to do"));}
    if(w=="template"){for(int i=0;i<TemplateCount;i++)if(rest==templateNames[i]){applyTemplate(i);return ok("template");}return "ERR sheet template empty|dnd|ret";}
    if(w=="export"){String v=exportText();return "{\"text\":"+jsonText(v.c_str())+",\"bytes\":"+String(v.length())+",\"parts\":"+String(chunks(v))+"}";}
    if(w=="import"){String error;return importText(rest,&error)?ok("imported"):"ERR "+error;}
    if(w=="put"){unsigned k=0,n=0;int at=0;if(sscanf(rest.c_str(),"%u/%u%n",&k,&n,&at)<2||!at||rest[at]!=' '||!k||k>n||n>MaxParts)return "ERR sheet put K/N TEXT";at++;
      if(k==1){upload="";uploadNext=1;}if(k!=uploadNext){uploadNext=0;upload="";return "ERR sheet put: part "+String(k)+" out of order";}
      upload+=rest.substring(at);uploadNext++;if(k<n)return "OK put "+String(k)+"/"+String(n);
      String text=upload,error;upload="";uploadNext=0;return importText(text,&error)?ok("imported"):"ERR "+error;}
    if(w=="send"){char* e=nullptr;uint64_t peer=strtoull(rest.c_str(),&e,16);String error;if(!rest.length()||!e||*e||!peer)return "ERR sheet send NODE_ID";return send(peer,&error)?ok("sending"):"ERR "+error;}
    if(w=="offer"){if(rest=="accept"){if(!offered)return "ERR no sheet offered";return acceptOffer()?ok("accepted"):"ERR "+why;}if(rest=="decline"){declineOffer();return ok("declined");}return "ERR sheet offer accept|decline";}
    if(number(w,a)){String v=take(rest);
      if(v.startsWith("+")||v.startsWith("-"))return number(v.substring(v[0]=='+'?1:0),b)&&change(a,b)?ok("changed"):"ERR no such value";
      if(v=="set")return number(rest,b)&&setValue(a,b)?ok("set"):"ERR sheet I set N";
      return "ERR sheet I +N|-N|set N";}
    return "ERR sheet [add|set|move|del|act|I +N|template|export|import|send|offer]";}
  if(op=="char"){String w=take(rest);
    if(w=="add")return addCharacter(rest)?ok("added"):"ERR 6 characters at most, name 1-23 bytes";
    if(w=="new"){String k=take(rest);for(int i=0;i<TemplateCount;i++)if(k==templateNames[i])return addCharacter(rest,i)?ok("added"):"ERR 6 characters at most, name 1-23 bytes";return "ERR char new empty|dnd|ret NAME";}
    if(w=="use")return number(rest,a)&&useCharacter(a)?ok("chosen"):"ERR no such character";
    if(w=="name")return renameCharacter(rest)?ok("renamed"):"ERR name: 1-23 bytes";
    if(w=="del")return deleteCharacter()?ok("deleted"):"ERR the general set stays";
    return "ERR char add|new|use|name|del";}
  return "ERR dice [roll|pool|mode|clear|saved|grid|sheet|char]";
}
}
#else
// The dice left out of this image (Modules.h): the command says so.
dice::Dicer dicer;
namespace dice {
void Dicer::begin(){}void Dicer::tick(){}void Dicer::save(){}
bool Dicer::receive(uint64_t,const char*,const char*){return false;}
bool Dicer::delivery(uint32_t,uint8_t){return false;}
String Dicer::json() const{return "{}";}
String Dicer::command(const String&){return "ERR dice are not in this build";}
}
#endif
