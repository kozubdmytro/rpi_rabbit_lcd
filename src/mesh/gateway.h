#pragma once
#include "management_state.h"
#include "duty_protocol.h"
#include "node_freshness.h"
#include "chip_temperature_protocol.h"
#include <array>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace sr::mesh {
using Bytes=std::vector<uint8_t>;
using UUID=std::array<uint8_t,16>;
int64_t monotonicUs();
struct Reading { bool valid=false; uint16_t address=0; int16_t temperature=0; uint16_t humidity=0; int64_t received=0; };
struct MenuEntry {
    unsigned type=0; std::string name; uint16_t address=0;
    bool operator==(const MenuEntry& other) const {return type==other.type&&name==other.name&&address==other.address;}
};
inline std::vector<MenuEntry> defaultMenu() {return {{0,"climate",0},{1,"soil",0},{2,"lights",0},{3,"plug",0},{4,"blinds",0},{5,"thermo",0}};}
struct View { std::vector<MenuEntry> menu=defaultMenu(); std::vector<Reading> readings; Reading reading; uint8_t rotation=3,brightness=50; std::string error,ip,wifiName; bool hasNodes=false; unsigned nodeCount=0; };
struct Node {
    uint16_t address=0; uint8_t elements=1,type=0; UUID uuid{};
    // Removal evidence is durable so a failed cleanup cannot permit address reuse.
    uint8_t removal=0; // 0 normal, 1 deletion requested, 2 backend deleted
    uint8_t evidence=0;
    bool configured=false,configurationAttempted=false,valid=false;
    int16_t temperature=0,chip=CHIP_TEMPERATURE_UNKNOWN;
    uint16_t humidity=0; uint8_t percent=255; int64_t received=0;
    duty_peer_t duty{};
};
// All protocol operations run on the Bluetooth worker. Only view() crosses threads.
class Gateway {
public:
    explicit Gateway(std::string stateDirectory,bool createNetwork);
    ~Gateway();
    Gateway(const Gateway&)=delete;
    Gateway& operator=(const Gateway&)=delete;
    UUID identity{}; uint64_t token=0;
    bool creating=false,ready=false;
    std::vector<Node> nodes;
    std::function<void(uint16_t,const Bytes&,bool)> send;
    std::function<void(const UUID&)> bind;
    std::function<void(uint16_t,uint8_t)> erase;
    std::function<void(bool,unsigned)> scan;
    std::function<void(uint16_t)> changed;
    // Platform handoff must finish before provisioning/configuration starts.
    std::function<bool()> configurationReady;
    bool configurationPending() const;
    void joined(uint64_t value);
    void attached();
    uint16_t reserve(uint8_t count);
    void observed(const uint8_t* uuid,size_t length);
    void provisioned(const UUID&,uint16_t,uint8_t);
    void provisionFailed();
    void deleted(uint16_t,bool);
    void message(uint16_t,uint16_t,const Bytes&,bool deviceKey=false);
    uint8_t command(const Bytes&,Bytes& inventory);
    uint8_t displayWrite(bool rotation,uint8_t value);
    std::string webState() const;
    uint8_t webRemove(uint16_t address,uint16_t requestId,bool forget=false);
    uint8_t menuWrite(const std::vector<MenuEntry>& entries);
    void networkAddress(const std::string& ip,const std::string& name="");
    Bytes data() const;
    Bytes status() const;
    void disconnect();
    void tick();
    void fault(const std::string& error);
    View view() const;
private:
    std::string directory;
    int lockFd=-1;
    uint16_t nextAddress=0x100;
    UUID pendingUUID{}; uint16_t reserved=0; uint8_t reservedCount=0;
    bool provisioning=false,scanActive=false;
    management_state_t management{};
    uint16_t configAddress=0; unsigned configStep=0,retries=0;
    int64_t configDeadline=0;
    mutable std::mutex viewMutex;
    View current;
    uint16_t latestBme=0,latestSoil=0;
    int64_t resetRetryAt=0;
    std::vector<MenuEntry> menu=defaultMenu();
    uint8_t rotation=3,brightness=50;
    Bytes bmeRecord,soilRecord;
    Node* find(uint16_t);
    void save();
    void publishView();
    Bytes inventory();
    void configure(uint16_t);
    void configSend();
    void configFinish(bool);
    void remove(Node&);
    void cleanup(Node&);
};
}
