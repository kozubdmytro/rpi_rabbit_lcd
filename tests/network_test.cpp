#include "wifi_credentials.h"
#include "mesh/management_state.h"
#include <iostream>
#include <stdexcept>

static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
    try {
        std::string field="old";
        check(sr::credentialFragment(field,{0,'n','e','w'},32)&&field=="new","offset zero replaces previous credentials");
        check(!sr::credentialFragment(field,{2,'x'},32)&&field=="new","out-of-order fragments are rejected without mutation");
        check(sr::credentialFragment(field,{3,'!'},32)&&field=="new!","contiguous fragments append");
        check(!sr::credentialFragment(field,{4,'x'},4)&&field=="new!","oversized fragments preserve the field");
        check(sr::credentialFragment(field,{0},64)&&field.empty(),"an explicit empty password selects an open network");
        check(!sr::validWifiCredentials("","password")&&!sr::validWifiCredentials("SSID","short"),"invalid SSID and short password rejected");
        check(sr::validWifiCredentials(std::string(32,'s'),std::string(63,'p')),"maximum WPA passphrase lengths accepted");
        check(sr::validWifiCredentials("SSID",std::string(64,'a'))&&!sr::validWifiCredentials("SSID",std::string(64,'z')),"64-byte keys must be hexadecimal");
        check(!sr::validWifiCredentials("SSID",std::string("pass\0word",9)),"embedded NUL is rejected");
        management_state_t state{};
        management_command_t command{};command.opcode=MANAGEMENT_RESET;command.request_id=1;command.target.address=256;
        uint8_t wire[]={1,MANAGEMENT_RESET,1,0,0,1};
        management_begin(&state,&command,wire,sizeof(wire),1);
        check(management_removal_started(&state,2)&&state.reset_pending&&!state.deletion_pending,"remove starts with remote reset only");
        management_tick(&state,2+MANAGEMENT_RESET_US,true);
        check(state.status.state==MANAGEMENT_FAILED&&!state.deletion_pending,"timeout never authorizes local forget");
        check(!management_reset_result(&state,256,true,3+MANAGEMENT_RESET_US)&&!state.deletion_pending,"late reset cannot authorize deletion");
        state={};management_begin(&state,&command,wire,sizeof(wire),1);management_removal_started(&state,2);
        check(!management_reset_result(&state,257,true,3),"another node's reset cannot authorize deletion");
        check(management_reset_result(&state,256,true,4)&&state.deletion_pending,"confirmed reset authorizes local key deletion");
        check(management_deletion_result(&state,256,true)&&state.cleanup_pending,"backend deletion authorizes journal cleanup");
        check(management_cleanup_result(&state,256,true)&&state.status.state==MANAGEMENT_COMPLETE,"removal completes after journal cleanup");
        std::cout<<"PASS: Wi-Fi fragments, credential validation, confirmed node removal\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
