// The quick counter of the 320x240 screens (included by Ui.cpp; People.h keeps the cards): the open card
// counts at once, OK or up +1, down -1; left and right turn the cards (swipes on the T-Deck), and the one
// after the last adds a card. N or a hold names it, C colours it, R twice resets it, DEL twice deletes it;
// B and W count the BLE and Wi-Fi devices nearby, shown on the card.
uint32_t tallyArmed=0;int tallyArmKey=0;
uint16_t tallyHue(unsigned i){return rgb(dice::colors[people::tally.list[i].color%dice::Colors]);}
bool tallyAdding(){return tallyNew&&people::tally.count<people::Counters;}
String tallyTileDetail(){return people::name(people::tally.current)+": "+String(people::value());}
void drawTally(){
 auto& d=g();const auto& c=people::tally;unsigned n=c.count;bool adding=tallyAdding();
 int total=n+(n<people::Counters),pos=adding?n:c.current;
 if(total>1){panel(8,36,13,134,card,5);panel(299,36,13,134,card,5);target(0,24,26,160,-KeyLeft);target(294,24,26,160,-KeyRight);
  int before=(pos+total-1)%total,after=(pos+1)%total;
  if(before<int(n))d.fillRoundRect(8,36,3,134,1,tallyHue(before));if(after<int(n))d.fillRoundRect(309,36,3,134,1,tallyHue(after));}
 if(adding){ring(26,26,268,154,10,line);d.fillRoundRect(146,82,28,5,2,accent);d.fillRoundRect(158,70,5,29,2,accent);textCenter(160,124,t("New counter","Новый счётчик"),ink,bold);
  textCenter(160,144,t("OK: add","OK: добавить"),dim,small);target(26,26,268,154,-Enter);}
 else{
  uint16_t hue=tallyHue(c.current);panel(26,26,268,154,card,10);d.fillRoundRect(26,26,268,6,3,hue);target(40,124,104,32,-KeyDown);target(176,124,104,32,-Enter);target(26,26,268,96,-Enter);
  String index=String(c.current+1)+"/"+String(n);textRight(282,52,index,dim,small);text(40,52,fit(people::name(c.current),230-measure(index,small),bold),ink,bold);
  String v=String(people::value());bool large=measure(v,digits)<=240;textCenter(160,large?112:100,v,ink,large?digits:big);
  panel(40,124,104,32,bg,8);textCenter(92,145,"-1",people::value()?ink:faint,bold);
  panel(176,124,104,32,mix(dice::colors[people::current().color%dice::Colors],0x131a22,.6f),8);textCenter(228,145,"+1",ink,bold);
  if(people::settings.ble||people::settings.wifi){String near=t("Nearby: ","Рядом: ");if(people::settings.ble)near+="BLE "+String(people::count(false))+(people::saturated(false)?"+":"");
   if(people::settings.wifi)near+=String(people::settings.ble?" · ":"")+"Wi-Fi "+String(people::count(true))+(people::saturated(true)?"+":"");textCenter(160,172,near,dim,small);}
 }
 // A dot for every card, the open one larger, and a "+" for a new one.
 int x=160-(total-1)*7;
 for(int i=0;i<total;i++,x+=14){bool on=i==pos;
  if(i<int(n))d.fillCircle(x,193,on?4:2,on?tallyHue(i):faint);
  else{uint16_t k=on?accent:faint;d.drawFastHLine(x-3,193,7,k);d.drawFastVLine(x,190,7,k);}}
 textCenter(160,211,t("N: name  C: colour  R: reset  DEL: delete  B: BLE","N: имя  C: цвет  R: сброс  DEL: удалить  B: BLE"),faint,small);
 if(adding)footer({{"OK",t("Add","Добавить")},{"<>",t("Cards","Карточки")},{"BACK",t("Exit","Выход")}});
 else footer({{"OK","+1"},{"^v","-1"},{"<>",t("Cards","Карточки")},{"N",t("Name","Имя")}});
}
// R and DEL ask again within five seconds.
bool tallyConfirm(int key){
 if(tallyArmKey==key&&millis()-tallyArmed<5000){tallyArmKey=0;return true;}
 tallyArmKey=key;tallyArmed=millis();notice(key==Erase?t("DEL again: delete","Ещё раз DEL: удалить"):t("R again: reset to 0","Ещё раз R: сбросить в 0"),warn);return false;
}
bool tallyKey(int key){
 auto& c=people::tally;unsigned n=c.count;int total=n+(n<people::Counters);
 if(key>='A'&&key<='Z')key+=32;
 if(key!=Erase&&key!='r')tallyArmKey=0;
 if(key==KeyLeft||key==KeyRight){int pos=tallyAdding()?n:c.current;pos=(pos+(key==KeyLeft?total-1:1))%total;tallyNew=pos==int(n);if(!tallyNew)people::select(pos);return true;}
 if(tallyAdding()||key=='a'){
  if(key==Enter||key==KeyHold||key=='a'){if(people::add()){tallyNew=false;notice(people::name(c.current),ok);}else notice(t("8 counters at most","Не больше 8 счётчиков"),warn);return true;}
  return key!=KeyBack;}
 tallyNew=false;
 if(key==Enter||key==KeyUp){people::change(1);return true;}
 if(key==KeyDown){people::change(-1);return true;}
 if(key==KeyHold||key=='n'){editing=true;edit=c.list[c.current].name;return true;}
 if(key=='c'){people::recolor(c.current,(people::current().color+1)%people::Colors);return true;}
 if(key=='r'){if(tallyConfirm('r')){people::set(c.current,0);notice(t("Counter reset","Счётчик сброшен"),dim);}return true;}
 if(key==Erase){if(n<2)notice(t("The last counter stays","Последний счётчик остаётся"),warn);else if(tallyConfirm(Erase)){people::remove(c.current);notice(t("Counter deleted","Счётчик удалён"),dim);}return true;}
 if(key=='b'||key=='w'){bool ble=key=='b',on=ble?!people::settings.ble:!people::settings.wifi;StaticJsonDocument<96>j;j["action"]="settings";j[ble?"ble":"wifi"]=on;
  String r=people::command(j.as<JsonObjectConst>());notice(r.startsWith("OK")?String(ble?"BLE":"Wi-Fi")+": "+(on?t("on","вкл."):t("off","выкл.")):r,r.startsWith("OK")?ok:bad);return true;}
 return key!=KeyBack;
}
void tallySaveName(){if(people::rename(people::tally.current,edit)){editing=false;notice(t("Name saved","Имя сохранено"),ok);}else notice(t("Name: up to 23 UTF-8 bytes","Имя: до 23 байт UTF-8"),bad);}
