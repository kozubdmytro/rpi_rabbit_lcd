#include "gateway.h"
#include "moisture_protocol.h"
#include "../web.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/random.h>
#include <unistd.h>

namespace sr::mesh {
int64_t monotonicUs() {return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void randomBytes(void* destination,size_t size) {
    auto p=static_cast<uint8_t*>(destination);
    while(size) {auto n=getrandom(p,size,0);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("Cannot obtain random identity");p+=n;size-=size_t(n);}
}
static void put16(Bytes& b,uint16_t v) {b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
static std::string hex(const UUID& u) {std::ostringstream s;for(auto b:u)s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return s.str();}
static UUID unhex(const std::string& s) {
    if(s.size()!=32||s.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid saved UUID");
    UUID u{};for(size_t i=0;i<u.size();++i)u[i]=uint8_t(std::stoul(s.substr(i*2,2),nullptr,16));return u;
}
Gateway::Gateway(std::string path,bool createNetwork):directory(std::move(path)) {
    std::filesystem::create_directories(directory);
    lockFd=open((directory+"/lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lockFd<0||flock(lockFd,LOCK_EX|LOCK_NB)<0) {if(lockFd>=0)close(lockFd);lockFd=-1;throw std::runtime_error("Gateway state is locked/unavailable");}
    try {
        auto state=directory+"/gateway.conf";
        if(std::filesystem::exists(state)) {
            if(createNetwork)throw std::runtime_error("Existing gateway state: refusing to create another network");
            std::ifstream f(state);std::string magic,id,pending;unsigned rotation,brightness,allocated,count;
            if(!(f>>magic>>id>>token>>allocated>>rotation>>brightness>>pending>>reserved>>count)||magic!="SRMESH1"||allocated<0x100||allocated>0x8000||rotation<1||rotation>4||brightness>100||count>255)
                throw std::runtime_error("Corrupt gateway state; no network was recreated");
            identity=unhex(id);pendingUUID=unhex(pending);nextAddress=uint16_t(allocated);reservedCount=uint8_t(count);
            this->rotation=uint8_t(rotation);this->brightness=uint8_t(brightness);
            current.rotation=this->rotation;current.brightness=this->brightness;
            std::string tag;bool menuSeen=false;
            while(f>>tag) {
                if(tag=="menu") {
                    unsigned type,address;std::string name;
                    if(!(f>>type>>address>>std::quoted(name))||type>5||address>32767||name.empty()||name.size()>16||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.")!=std::string::npos)
                        throw std::runtime_error("Corrupt menu state");
                    if(!menuSeen) {menu.clear();menuSeen=true;}
                    if(menu.size()>=16)throw std::runtime_error("Too many menu entries");
                    menu.push_back({type,name,uint16_t(address)});continue;
                }
                if(tag=="empty-menu") {if(menuSeen)throw std::runtime_error("Duplicate menu state");menu.clear();menuSeen=true;continue;}
                Node n;unsigned address,elements,type,removal,evidence;std::string uuid;
                if(tag!="node"||!(f>>address>>elements>>type>>removal>>evidence>>uuid)||!address||address>0x7fff||!elements||elements>255||address+elements>0x8000||type>2||removal>2||evidence>127||nodes.size()>=10)
                    throw std::runtime_error("Corrupt node state");
                n.address=uint16_t(address);n.elements=uint8_t(elements);n.type=uint8_t(type);n.removal=uint8_t(removal);n.evidence=uint8_t(evidence);n.uuid=unhex(uuid);
                for(const auto& existing:nodes)if(existing.uuid==n.uuid||(address<unsigned(existing.address)+existing.elements&&existing.address<address+elements))throw std::runtime_error("Overlapping saved nodes");
                nodes.push_back(n);
            }
            for(const auto& n:nodes)if(unsigned(n.address)+n.elements>nextAddress)throw std::runtime_error("Saved allocation overlaps known nodes");
            if(!f.eof()||!token||reserved||reservedCount||pendingUUID!=UUID{})throw std::runtime_error("Incomplete network/provisioning journal: operator recovery required; keys were retained");
        } else {
            if(!createNetwork)throw std::runtime_error("No Mesh state; first launch requires --create-network (pairs no nodes automatically)");
            randomBytes(identity.data(),identity.size());identity[6]=(identity[6]&15)|64;identity[8]=(identity[8]&63)|128;
            creating=true;save();
        }
        randomBytes(&management.status.boot_id,sizeof(management.status.boot_id));
        publishView();
    } catch(...) {close(lockFd);lockFd=-1;throw;}
}
Gateway::~Gateway() {if(lockFd>=0)close(lockFd);}
void Gateway::save() {
    std::ostringstream s;s<<"SRMESH1 "<<hex(identity)<<' '<<token<<' '<<nextAddress<<' '<<unsigned(rotation)<<' '<<unsigned(brightness)<<' '<<hex(pendingUUID)<<' '<<reserved<<' '<<unsigned(reservedCount)<<'\n';
    for(const auto& n:nodes)s<<"node "<<n.address<<' '<<unsigned(n.elements)<<' '<<unsigned(n.type)<<' '<<unsigned(n.removal)<<' '<<unsigned(n.evidence)<<' '<<hex(n.uuid)<<'\n';
    if(menu.empty())s<<"empty-menu\n";
    for(const auto& entry:menu)s<<"menu "<<entry.type<<' '<<entry.address<<' '<<std::quoted(entry.name)<<'\n';
    std::string temp=directory+"/gateway.tmp-XXXXXX";int fd=mkstemp(temp.data());if(fd<0)throw std::runtime_error("Cannot create gateway journal");
    try {
        auto bytes=s.str();size_t offset=0;
        while(offset<bytes.size()) {auto n=write(fd,bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("Cannot write gateway journal");offset+=size_t(n);}
        if(fsync(fd)<0)throw std::runtime_error("Cannot sync gateway journal");close(fd);fd=-1;
        if(rename(temp.c_str(),(directory+"/gateway.conf").c_str())<0)throw std::runtime_error("Cannot replace gateway journal");
        int d=open(directory.c_str(),O_DIRECTORY|O_RDONLY|O_CLOEXEC);if(d<0)throw std::runtime_error("Cannot open gateway directory");int result=fsync(d);close(d);if(result<0)throw std::runtime_error("Cannot sync gateway directory");
    } catch(...) {if(fd>=0)close(fd);/* Keep failed journal for diagnosis; never delete Mesh state. */throw;}
}
Node* Gateway::find(uint16_t a) {for(auto& n:nodes)if(n.address==a)return &n;return nullptr;}
void Gateway::joined(uint64_t value) {token=value;save();creating=false;}
void Gateway::attached() {ready=true;}
void Gateway::fault(const std::string& error) {ready=false;std::lock_guard<std::mutex> guard(viewMutex);current.error=error;}
View Gateway::view() const {std::lock_guard<std::mutex> guard(viewMutex);return current;}
void Gateway::publishView() {
    std::lock_guard<std::mutex> guard(viewMutex);auto n=find(latestBme);current.reading={};
    current.nodeCount=unsigned(std::count_if(nodes.begin(),nodes.end(),[](const Node& node){return !node.removal;}));
    current.hasNodes=current.nodeCount!=0;
    current.menu=menu;current.readings.clear();
    for(const auto& node:nodes)if(!node.removal&&node.type==1)current.readings.push_back({node.valid,node.address,node.temperature,node.humidity,node.received});
    if(n&&n->valid&&n->type==1)current.reading={true,n->address,n->temperature,n->humidity,n->received};
}
uint8_t Gateway::displayWrite(bool rotation,uint8_t value) {
    if((rotation&&(value<1||value>4))||(!rotation&&value>100))return 0xff;
    auto& field=rotation?this->rotation:brightness;
    if(field==value)return 0;auto old=field;field=value;
    try {save();}catch(const std::exception& e) {field=old;std::cerr<<"Cannot persist display setting: "<<e.what()<<'\n';return 0x0e;}
    {std::lock_guard<std::mutex> guard(viewMutex);current.rotation=this->rotation;current.brightness=brightness;}return 0;
}
uint8_t Gateway::menuWrite(const std::vector<MenuEntry>& entries) {
    if(entries.size()>16)return 0xff;
    for(const auto& entry:entries) {
        if(entry.type>5||entry.name.empty()||entry.name.size()>16||entry.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.")!=std::string::npos)return 0xff;
        if(entry.address) {auto n=find(entry.address);if(entry.type!=0||!n||n->removal||n->type!=1)return 0xff;}
    }
    auto old=menu;menu=entries;try {save();}catch(...) {menu=old;return 0x0e;}publishView();return 0;
}
uint16_t Gateway::reserve(uint8_t count) {
    if(!provisioning||!count||reservedCount||uint32_t(nextAddress)+count>0x8000)throw std::runtime_error("Invalid provisioning address request");
    reserved=nextAddress;reservedCount=count;nextAddress=uint16_t(nextAddress+count);save();return reserved;
}
void Gateway::observed(const uint8_t* uuid,size_t length) {
    if(length<16||uuid[0]!=0xaa||uuid[1]!=0xbb)return;
    management_candidate_t c{};memcpy(c.uuid,uuid,16);management_observe(&management,&c,monotonicUs());
}
void Gateway::provisioned(const UUID& uuid,uint16_t address,uint8_t count) {
    if(!provisioning||uuid!=pendingUUID||address!=reserved||count!=reservedCount||!count||find(address)||nodes.size()>=10)throw std::runtime_error("Unexpected provisioning completion; retaining journal");
    Node n;n.address=address;n.elements=count;n.uuid=uuid;n.configurationAttempted=true;nodes.push_back(n);reserved=0;reservedCount=0;pendingUUID={};save();provisioning=false;
    publishView();
    if(management_provisioned(&management,uuid.data(),address,monotonicUs()))configure(address);
}
void Gateway::provisionFailed() {
    provisioning=false;reserved=0;reservedCount=0;pendingUUID={};save();management_result(&management,MANAGEMENT_ERROR_PROVISIONING);
}
Bytes Gateway::status() const {Bytes b(20);management_status_bytes(&management,monotonicUs(),b.data());return b;}
Bytes Gateway::inventory() {
    Bytes b(8);b[0]=1;b[2]=management.overflow?1:0;if(!++management.status.snapshot_id)++management.status.snapshot_id;management_write_u32(b.data()+4,management.status.snapshot_id);
    auto now=monotonicUs();
    for(const auto& n:nodes) {
        bool fresh=node_is_fresh(n.valid,n.received,now);b.push_back(!n.configured?5:!n.valid?1:!fresh?4:n.duty.armed&&now<n.duty.ready_us+DUTY_SLEEP_US?3:2);
        b.push_back(n.type);put16(b,n.address);b.insert(b.end(),n.uuid.begin(),n.uuid.end());++b[1];
    }
    for(const auto& c:management.candidates) {
        if(!management_candidate_fresh(&c,now))continue;
        bool known=false;for(const auto& n:nodes)if(!memcmp(n.uuid.data(),c.uuid,16))known=true;if(known)continue;
        b.insert(b.end(),4,0);b.insert(b.end(),c.uuid,c.uuid+16);++b[1];
    }return b;
}
Bytes Gateway::data() const {
    Bytes b=bmeRecord;b.insert(b.end(),soilRecord.begin(),soilRecord.end());return b;
}
void Gateway::networkAddress(const std::string& ip,const std::string& name) {std::lock_guard<std::mutex> guard(viewMutex);current.ip=ip;current.wifiName=ip.empty()?"":name;}
std::string Gateway::webState() const {
    auto now=monotonicUs();auto s=status();std::ostringstream out;
    out<<"{\"ready\":"<<(ready?"true":"false")<<",\"brightness\":"<<unsigned(brightness)<<",\"rotation\":"<<unsigned(rotation)
       <<",\"operation\":{\"id\":"<<management_read_u16(s.data()+2)<<",\"state\":"<<unsigned(s[4])<<",\"error\":"<<unsigned(s[5])
       <<",\"flags\":"<<unsigned(s[8])<<",\"scanSeconds\":"<<unsigned(s[9])<<"},\"nodes\":[";
    bool comma=false;
    for(const auto& n:nodes) {
        if(comma)out<<',';comma=true;
        out<<"{\"address\":"<<n.address<<",\"uuid\":\""<<hex(n.uuid)<<"\",\"type\":"<<unsigned(n.type)
           <<",\"configured\":"<<(n.configured?"true":"false")<<",\"fresh\":"<<(node_is_fresh(n.valid,n.received,now)?"true":"false")
           <<",\"temperature\":"<<(n.valid&&n.type==1?std::to_string(n.temperature/100.0):"null")<<",\"humidity\":"<<(n.valid&&n.type==1?std::to_string(n.humidity/100.0):"null")
           <<",\"removing\":"<<(n.removal?"true":"false")<<'}';
    }
    out<<"],\"menu\":[";comma=false;
    for(const auto& entry:menu) {if(comma)out<<',';comma=true;out<<"{\"type\":"<<entry.type<<",\"name\":"<<sr::jsonString(entry.name)<<",\"address\":"<<entry.address<<'}';}
    out<<"],\"candidates\":[";comma=false;
    for(const auto& c:management.candidates) {
        if(!management_candidate_fresh(&c,now))continue;
        bool known=false;for(const auto& n:nodes)if(!memcmp(n.uuid.data(),c.uuid,16))known=true;if(known)continue;
        UUID id;std::copy(c.uuid,c.uuid+16,id.begin());if(comma)out<<',';comma=true;out<<'"'<<hex(id)<<'"';
    }
    out<<"]}";return out.str();
}
uint8_t Gateway::webRemove(uint16_t address,uint16_t requestId,bool forget) {
    auto n=find(address);if(!n)return MANAGEMENT_ATT_NOT_FOUND;
    // Ordinary remove requires remote reset; explicit forget permits local-only removal.
    uint8_t opcode=forget||(n->removal&&(n->evidence&MANAGEMENT_FLAG_RESET_CONFIRMED))?MANAGEMENT_FORGET:MANAGEMENT_RESET;
    Bytes commandBytes={1,opcode,uint8_t(requestId),uint8_t(requestId>>8),uint8_t(address),uint8_t(address>>8)},snapshot;
    return command(commandBytes,snapshot);
}
uint8_t Gateway::command(const Bytes& b,Bytes& snapshot) {
    management_command_t c{};auto decoded=management_decode_command(b.data(),b.size(),&c);
    if(decoded!=MANAGEMENT_DECODE_OK)return decoded==MANAGEMENT_DECODE_INVALID_LENGTH?0x0d:decoded==MANAGEMENT_DECODE_UNSUPPORTED?6:0xff;
    int replay=management_replay(&management,b.data(),b.size(),c.request_id);if(replay)return replay>0?0:MANAGEMENT_ATT_CONFLICT;
    if(c.opcode==MANAGEMENT_SCAN_STOP) {management_stop_scan(&management);return 0;}
    if(c.opcode==MANAGEMENT_CAPTURE) {snapshot=inventory();return 0;}
    if(!ready)return MANAGEMENT_ATT_NOT_READY;
    if(management.owned||(c.opcode!=MANAGEMENT_SCAN_START&&(configAddress||provisioning)))return MANAGEMENT_ATT_BUSY;
    if(c.opcode==MANAGEMENT_BIND) {
        for(const auto& n:nodes)if(n.removal)return MANAGEMENT_ATT_BUSY;
        auto candidate=management_find_candidate(&management,c.target.uuid,monotonicUs());if(!candidate)return MANAGEMENT_ATT_NOT_FOUND;
        for(const auto& n:nodes)if(!memcmp(n.uuid.data(),c.target.uuid,16))return MANAGEMENT_ATT_NOT_FOUND;
        if(nodes.size()>=10)return MANAGEMENT_ATT_CAPACITY;
    } else if(c.opcode>=MANAGEMENT_RETRY_CONFIG) {
        auto n=find(c.target.address);if(!n||(c.opcode!=MANAGEMENT_FORGET&&n->removal))return MANAGEMENT_ATT_NOT_FOUND;
    }
    management_begin(&management,&c,b.data(),b.size(),monotonicUs());return 0;
}
void Gateway::disconnect() {management_stop_scan(&management);}
bool Gateway::configurationPending() const {
    if(provisioning||configAddress)return true;
    if(management.owned&&management.pending&&management.status.state!=MANAGEMENT_FAILED&&
       (management.command.opcode==MANAGEMENT_BIND||management.command.opcode==MANAGEMENT_RETRY_CONFIG))return true;
    if(ready&&!management.owned)for(const auto& n:nodes)if(!n.configurationAttempted&&!n.removal)return true;
    return false;
}
void Gateway::configure(uint16_t address) {configAddress=address;configStep=0;retries=0;find(address)->configured=false;find(address)->configurationAttempted=true;configSend();}
void Gateway::configSend() {
    Bytes b;
    if(configStep==0) {b={0x00,0,0,0};/* AppKey Add uses backend's local key API. */}
    else if(configStep==1) {b={0x80,0x3d};put16(b,configAddress);put16(b,0);put16(b,SENSOR_COMPANY_ID);put16(b,SENSOR_SERVER_MODEL_ID);}
    else {b={0x03};put16(b,configAddress);put16(b,1);put16(b,0);b.insert(b.end(),{7,0,0});put16(b,SENSOR_COMPANY_ID);put16(b,SENSOR_SERVER_MODEL_ID);}
    configDeadline=monotonicUs()+5000000;send(configAddress,b,true);
}
void Gateway::configFinish(bool success) {
    if(auto n=find(configAddress))n->configured=success&&management.status.state!=MANAGEMENT_FAILED;
    if(management.owned&&management.status.address==configAddress)management_result(&management,success?0:MANAGEMENT_ERROR_CONFIGURING);
    configAddress=0;configDeadline=0;
}
void Gateway::remove(Node& n) {n.removal=1;save();erase(n.address,n.elements);}
void Gateway::cleanup(Node& n) {
    auto address=n.address;auto uuid=n.uuid;
    auto saved=nodes;auto savedMenu=menu;for(auto& entry:menu)if(entry.address==address)entry.address=0;nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const Node& x){return x.address==address;}),nodes.end());
    try {save();}catch(...) {nodes=std::move(saved);menu=std::move(savedMenu);management_cleanup_result(&management,address,false);throw;}
    for(auto& c:management.candidates)if(!memcmp(c.uuid,uuid.data(),16))c.used=false;
    if(latestBme==address) {latestBme=0;bmeRecord.clear();}
    publishView();
    if(latestSoil==address) {latestSoil=0;soilRecord.clear();}
    management_cleanup_result(&management,address,true);if(changed)changed(0xa005);
}
void Gateway::deleted(uint16_t address,bool success) {
    auto n=find(address);if(!n||!management_deletion_result(&management,address,success))return;
    n->removal=2;save();cleanup(*n);
}
void Gateway::message(uint16_t source,uint16_t key,const Bytes& bytes,bool deviceKey) {
    auto n=find(source);if(!n||bytes.empty()||key!=0||n->removal)return;auto now=monotonicUs();management_tick(&management,now,ready);
    if(deviceKey) {
        if(bytes.size()==2&&bytes[0]==0x80&&bytes[1]==0x4a) {
            if(management_reset_result(&management,source,true,now)) {n->evidence=MANAGEMENT_FLAG_RESET_CONFIRMED;remove(*n);}return;
        }
        if(configAddress!=source)return;
        size_t statusAt=0;
        if(configStep==0&&bytes.size()==6&&bytes[0]==0x80&&bytes[1]==0x03)statusAt=2;
        else if(configStep==1&&bytes.size()==11&&bytes[0]==0x80&&bytes[1]==0x3e&&management_read_u16(bytes.data()+3)==source&&management_read_u16(bytes.data()+5)==0&&management_read_u16(bytes.data()+7)==SENSOR_COMPANY_ID&&management_read_u16(bytes.data()+9)==SENSOR_SERVER_MODEL_ID)statusAt=2;
        else if(configStep==2&&bytes.size()==16&&bytes[0]==0x80&&bytes[1]==0x19&&management_read_u16(bytes.data()+3)==source&&management_read_u16(bytes.data()+5)==1&&management_read_u16(bytes.data()+12)==SENSOR_COMPANY_ID&&management_read_u16(bytes.data()+14)==SENSOR_SERVER_MODEL_ID)statusAt=2;
        if(!statusAt)return;
        if(management.status.state==MANAGEMENT_FAILED&&management.status.address==source) {configFinish(false);return;}
        if(bytes[statusAt]) {configFinish(false);return;}
        retries=0;if(++configStep==3)configFinish(true);else configSend();return;
    }
    if(bytes.size()<3||bytes[1]!=0x34||bytes[2]!=0x12)return;
    uint8_t op=bytes[0]&0x3f;if(bytes[0]<0xc0)return;const uint8_t* p=bytes.data()+3;size_t length=bytes.size()-3;
    if(op==0x0b) {if(length==5)duty_ready(&n->duty,duty_id(p),p[4],now);return;}
    uint8_t kind=op==8?1:op==9?2:0;uint32_t id=0;int16_t chip=CHIP_TEMPERATURE_UNKNOWN;
    if(kind) {size_t base=kind==1?8:7;if(!chip_temperature_decode_tail(p,length,base,&chip))return;id=duty_id(p);p+=4;length=base-4;}
    int16_t temperature=0;uint16_t humidity=0;uint8_t percent=255,type=0;
    if(op==3||kind==1) {if(!sensor_decode(p,length,&temperature,&humidity))return;type=1;}
    else if(op==5||kind==2) {if(!moisture_decode(p,length,&humidity,&percent))return;type=2;}else return;
    bool fresh=!kind||!n->duty.seen||n->duty.id!=id||n->duty.kind!=kind;
    if(fresh) {
        bool typeChanged=n->type!=type;n->type=type;n->temperature=temperature;n->humidity=humidity;n->percent=percent;n->chip=chip;n->received=now;n->valid=true;
        if(typeChanged)save();
        Bytes record={uint8_t(type==1?0x11:0x12),uint8_t(type==1?4:5)};put16(record,source);
        if(type==1) {int whole=(temperature>=0?temperature+50:temperature-50)/100;record.push_back(uint8_t(int8_t(whole)));record.push_back(uint8_t((humidity+50)/100));latestBme=source;bmeRecord=record;publishView();}
        else {put16(record,humidity);record.push_back(percent);latestSoil=source;soilRecord=record;}
        if(changed)changed(0xa005);
    }
    // A sleeping sensor is awake now; sending a sensor ACK first could let it sleep again.
    if(management.reset_pending&&management.status.state==MANAGEMENT_RESETTING&&management.status.address==source) {
        send(source,{0x80,0x49},true);resetRetryAt=now+2000000;return;
    }
    duty_report(&n->duty,id,kind,now);
    if(configAddress==source)return;
    Bytes reply={uint8_t(kind?0xca:type==1?0xc4:0xc6),0x34,0x12};
    if(kind) {size_t start=reply.size();reply.resize(start+5);duty_put_id(reply.data()+start,id);reply.back()=kind;}
    else reply.insert(reply.end(),p,p+length);send(source,reply,false);
}
void Gateway::tick() {
    auto now=monotonicUs();management_tick(&management,now,ready);
    bool wanted=management.scanning&&management.scan_ready;
    if(wanted!=scanActive) {scanActive=wanted;scan(wanted,wanted?management.command.target.scan_seconds:0);}
    if(configAddress&&now>=configDeadline) {
        if(management.status.state==MANAGEMENT_FAILED&&management.status.address==configAddress)configFinish(false);
        else if(retries++<3)configSend();else configFinish(false);
    }
    if(management.owned&&management.pending&&management.command.opcode!=MANAGEMENT_SCAN_START) {
        auto opcode=management.command.opcode;
        if(management.status.state==MANAGEMENT_FAILED)management.pending=false;
        else if(opcode==MANAGEMENT_BIND) {
            if(configurationReady&&!configurationReady())return;
            management.pending=false;management.status.state=MANAGEMENT_PROVISIONING;
            memcpy(pendingUUID.data(),management.command.target.uuid,16);provisioning=true;save();bind(pendingUUID);
        } else if(opcode==MANAGEMENT_RETRY_CONFIG) {
            if(configurationReady&&!configurationReady())return;
            management.pending=false;management.status.state=MANAGEMENT_CONFIGURING;configure(management.status.address);
        }
        else if(management_removal_started(&management,now)) {
            auto n=find(management.status.address);
            if(!n)throw std::runtime_error("Reserved removal target disappeared");
            if(opcode==MANAGEMENT_FORGET) {
                if(n->removal==2) {management_resume_cleanup(&management,n->evidence);cleanup(*n);}else remove(*n);
            } else {send(n->address,{0x80,0x49},true);resetRetryAt=now+2000000;}
        }
    }
    if(management.reset_pending&&management.status.state==MANAGEMENT_RESETTING&&now<management.deadline_us&&now>=resetRetryAt) {
        send(management.status.address,{0x80,0x49},true);resetRetryAt=now+2000000;
    }
    // Reset watchdog drains its own timed-out response window; late success is ignored.
    if(management.reset_pending&&now>=management.deadline_us)management.reset_pending=false;
    management_release(&management,provisioning||configAddress);
    if(ready&&!management.owned&&!configAddress&&!provisioning)for(const auto& n:nodes)if(!n.configurationAttempted&&!n.removal) {
        if(!configurationReady||configurationReady())configure(n.address);break;
    }
    if(changed)changed(0xa00b);
}
}
