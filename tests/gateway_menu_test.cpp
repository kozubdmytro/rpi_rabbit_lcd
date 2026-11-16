#include "mesh/gateway.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <iostream>
using namespace sr::mesh;
int main() {
 char path[]="/tmp/sr-menu-test-XXXXXX";assert(mkdtemp(path));
 {Gateway g(path,true);g.joined(42);g.attached();
  Node n;n.address=g.nodes.empty()?0x100:0;n.type=1;n.configurationAttempted=true;g.nodes.push_back(n);
  // RAM-only node fixture is removed before reloading the saved allocation.
  assert(g.menuWrite({{0,"Room",0x100},{2,"Lamp",0},{2,"Desk",0}})==0);
  assert(g.view().menu.size()==3&&g.view().menu[0].address==0x100);
  assert(g.menuWrite({{0,"Bad",0x101}})!=0);
  assert(g.menuWrite({{6,"Bad",0}})!=0);
  assert(g.menuWrite({{0,"bad\nname",0}})!=0);
  assert(g.view().menu[0].name=="Room");
  g.send=[](uint16_t,const Bytes&,bool){};
  g.message(0x100,0,{0xc3,0x34,0x12,0xd2,0x04,0x88,0x13});
  assert(g.view().readings.size()==1&&g.view().readings[0].temperature==1234);
  int resets=0,deletes=0;
  g.configurationReady=[] {return false;};
  g.send=[&](uint16_t,const Bytes& b,bool){if(b==Bytes{0x80,0x49})++resets;};
  g.erase=[&](uint16_t,uint8_t){++deletes;};g.scan=[](bool,unsigned){};
  assert(g.webRemove(0x100,1)==0);g.tick();assert(resets==1&&deletes==0);
  std::this_thread::sleep_for(std::chrono::milliseconds(2050));g.tick();assert(resets==2&&deletes==0);
  g.message(0x100,0,{0xc3,0x34,0x12,0xd2,0x04,0x88,0x13});assert(resets==3&&deletes==0);
  g.message(0x100,0,{0x80,0x4a},true);assert(deletes==1);
  g.deleted(0x100,true);assert(g.nodes.empty()&&g.view().menu[0].address==0);
 }
 {Gateway g(path,false);assert(g.view().menu.size()==3&&g.view().menu[0].name=="Room");assert(g.menuWrite({})==0);}
 {Gateway g(path,false);assert(g.view().menu.empty());assert(g.menuWrite(defaultMenu())==0);}
 {Gateway g(path,false);g.attached();Node n;n.address=0x100;n.configurationAttempted=true;g.nodes.push_back(n);
  g.scan=[](bool,unsigned){};g.erase=[&](uint16_t a,uint8_t){g.deleted(a,true);};g.send=[](uint16_t,const Bytes&,bool){assert(false);};
  assert(g.webRemove(0x100,2,true)==0);g.tick();assert(g.nodes.empty());
 }
 std::cout<<"PASS menu persistence/validation, assigned readings, reset retry/confirmation, explicit forget\n";
}
