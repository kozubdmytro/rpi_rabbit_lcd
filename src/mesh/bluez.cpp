#include "bluez.h"
#include "../wifi_credentials.h"
#include <gio/gio.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <functional>
#include <deque>
#include <iostream>
#include <map>
#include <stdexcept>
#include <future>
#include <sstream>
#include <ifaddrs.h>
#include <arpa/inet.h>

namespace sr::mesh {
namespace {
constexpr const char* meshRoot="/com/srlab/mesh";
constexpr const char* meshApp="/com/srlab/mesh/application";
constexpr const char* element="/com/srlab/mesh/element";
constexpr const char* phoneRoot="/com/srlab/phone";
constexpr const char* service="/com/srlab/phone/service";
constexpr const char* advertisement="/com/srlab/advertisement";
constexpr const char* network="/org/bluez/mesh";
GVariant* dict() {GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE_VARDICT);return g_variant_builder_end(&b);}
GVariant* array(const Bytes& b) {return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,b.data(),b.size(),1);}
Bytes bytes(GVariant* v) {gsize length=0;auto p=static_cast<const uint8_t*>(g_variant_get_fixed_array(v,&length,1));return length?Bytes(p,p+length):Bytes{};}
GVariant* at(GVariant* v,gsize i) {return g_variant_get_child_value(v,i);}
Bytes byteChild(GVariant* v,gsize i) {auto child=at(v,i);auto b=bytes(child);g_variant_unref(child);return b;}
const char* uuid(unsigned id) {thread_local char value[37];g_snprintf(value,sizeof(value),"0000%04x-0000-1000-8000-00805f9b34fb",id);return value;}
std::string charPath(unsigned id) {char suffix[12];g_snprintf(suffix,sizeof(suffix),"/c%04x",id);return std::string(service)+suffix;}
unsigned characteristic(const char* path) {auto p=std::strrchr(path,'/');return p&&p[1]=='c'?unsigned(std::strtoul(p+2,nullptr,16)):0;}
const char* xml=R"XML(<node>
<interface name="org.freedesktop.DBus.ObjectManager"><method name="GetManagedObjects"><arg type="a{oa{sa{sv}}}" direction="out"/></method></interface>
<interface name="org.bluez.mesh.Application1">
 <method name="JoinComplete"><arg type="t" direction="in"/></method><method name="JoinFailed"><arg type="s" direction="in"/></method>
 <property name="CompanyID" type="q" access="read"/><property name="ProductID" type="q" access="read"/><property name="VersionID" type="q" access="read"/><property name="CRPL" type="q" access="read"/>
</interface>
<interface name="org.bluez.mesh.Provisioner1">
 <method name="ScanResult"><arg type="n" direction="in"/><arg type="ay" direction="in"/><arg type="a{sv}" direction="in"/></method>
 <method name="RequestProvData"><arg type="y" direction="in"/><arg type="q" direction="out"/><arg type="q" direction="out"/></method>
 <method name="AddNodeComplete"><arg type="ay" direction="in"/><arg type="q" direction="in"/><arg type="y" direction="in"/></method>
 <method name="AddNodeFailed"><arg type="ay" direction="in"/><arg type="s" direction="in"/></method>
</interface>
<interface name="org.bluez.mesh.ProvisionAgent1"><property name="Capabilities" type="as" access="read"/></interface>
<interface name="org.bluez.mesh.Element1">
 <method name="MessageReceived"><arg type="q" direction="in"/><arg type="q" direction="in"/><arg type="v" direction="in"/><arg type="ay" direction="in"/></method>
 <method name="DevKeyMessageReceived"><arg type="q" direction="in"/><arg type="b" direction="in"/><arg type="q" direction="in"/><arg type="ay" direction="in"/></method>
 <method name="UpdateModelConfiguration"><arg type="q" direction="in"/><arg type="a{sv}" direction="in"/></method>
 <property name="Index" type="y" access="read"/><property name="Models" type="a(qa{sv})" access="read"/><property name="VendorModels" type="a(qqa{sv})" access="read"/>
</interface>
<interface name="org.bluez.GattService1"><property name="UUID" type="s" access="read"/><property name="Primary" type="b" access="read"/></interface>
<interface name="org.bluez.GattCharacteristic1">
 <method name="ReadValue"><arg type="a{sv}" direction="in"/><arg type="ay" direction="out"/></method>
 <method name="WriteValue"><arg type="ay" direction="in"/><arg type="a{sv}" direction="in"/></method>
 <method name="StartNotify"/><method name="StopNotify"/>
 <property name="UUID" type="s" access="read"/><property name="Service" type="o" access="read"/><property name="Flags" type="as" access="read"/>
 <property name="Value" type="ay" access="read"/><property name="Notifying" type="b" access="read"/>
</interface>
<interface name="org.bluez.LEAdvertisement1"><method name="Release"/>
 <property name="Type" type="s" access="read"/><property name="ServiceUUIDs" type="as" access="read"/><property name="LocalName" type="s" access="read"/>
</interface></node>)XML";
}
struct BluezGateway::Impl {
    Gateway gateway;
    std::string adapter,nodePath,meshOwner,phoneOwner,phone;
    std::atomic<bool> stopping{false};
    std::atomic<int> requestedBrightness{-1};
    std::thread worker;
    GMainContext* context=nullptr;
    GDBusConnection* bus=nullptr;
    GDBusNodeInfo* info=nullptr;
    GCancellable* cancel=nullptr;
    std::vector<guint> registrations,subscriptions;
    std::vector<std::pair<std::string,std::string>> objects;
    std::map<unsigned,bool> notifying;
    std::map<unsigned,Bytes> lastValue;
    Bytes frozen=Bytes(8);
    bool registering=false,advertised=false,addingLocalKey=false,localBinding=false,settingTtl=false;
    bool phonePaused=false,phoneRegistered=false,phoneRegistering=false,phoneUnregistering=false;
    bool advertisingStopping=false,phoneDisconnecting=false;
    bool reopeningMesh=false,freshMeshSession=false;
    struct Outgoing {uint16_t destination;Bytes bytes;bool deviceKey;};
    std::deque<Outgoing> outgoing;
    int64_t advertiseAfter=0;
    int64_t phonePauseAfter=0,phonePauseDeadline=0;
    int64_t localDeadline=0;
    std::string wifiDevice,wifiIp,wifiError,ssid,password;
    bool wifiOnline=false,wifiPolling=false,wifiConnecting=false,ssidWritten=false,passwordWritten=false;
    int64_t wifiPollAfter=0,wifiConnectDeadline=0;
    struct WebRequest {std::string path,body;std::promise<WebReply> result;};
    std::mutex webMutex;
    std::deque<std::shared_ptr<WebRequest>> webRequests;
    unsigned pending=0;
    Impl(const std::string& directory,const std::string& phoneAdapter,bool create):gateway(directory,create),adapter("/org/bluez/"+phoneAdapter) {
        if(phoneAdapter.rfind("hci",0)!=0||phoneAdapter.size()<4||phoneAdapter.find_first_not_of("0123456789",3)!=std::string::npos)throw std::runtime_error("Invalid phone adapter");
        frozen[0]=1;worker=std::thread([this]{run();});
    }
    ~Impl() {stopping=true;if(worker.joinable())worker.join();}
    void fail(const std::string& error) {std::cerr<<"BLE gateway: "<<error<<'\n';gateway.fault(error);stopping=true;}
    using Done=std::function<void(GVariant*,const GError*)>;
    void call(const char* destination,const std::string& path,const char* iface,const char* method,GVariant* args,Done done={}) {
        struct Request {Impl* self;Done done;std::string operation;};
        ++pending;
        g_dbus_connection_call(bus,destination,path.c_str(),iface,method,args,nullptr,G_DBUS_CALL_FLAGS_NONE,10000,cancel,
            [](GObject* source,GAsyncResult* result,gpointer pointer) {
                std::unique_ptr<Request> request(static_cast<Request*>(pointer));GError* error=nullptr;
                auto reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&error);auto self=request->self;
                if(!self->stopping&&!self->reopeningMesh)try {if(request->done)request->done(reply,error);else if(error)self->fail(request->operation+": "+error->message);}catch(const std::exception& e){self->fail(request->operation+": "+e.what());}
                if(reply)g_variant_unref(reply);if(error)g_error_free(error);--self->pending;
            },new Request{this,std::move(done),std::string(iface)+"."+method});
    }
    void meshCall(const char* method,GVariant* args,Done done={}) {call("org.bluez.mesh",nodePath,"org.bluez.mesh.Management1",method,args,std::move(done));}
    GVariant* properties(const char* path,const char* iface) {
        GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE_VARDICT);
        auto add=[&](const char* key,GVariant* value){g_variant_builder_add(&b,"{sv}",key,value);};
        std::string i=iface;
        if(i=="org.bluez.mesh.Application1") {add("CompanyID",g_variant_new_uint16(0x1234));add("ProductID",g_variant_new_uint16(1));add("VersionID",g_variant_new_uint16(1));add("CRPL",g_variant_new_uint16(64));}
        else if(i=="org.bluez.mesh.ProvisionAgent1")add("Capabilities",g_variant_new_strv(nullptr,0));
        else if(i=="org.bluez.mesh.Element1") {
            add("Index",g_variant_new_byte(0));GVariantBuilder models;g_variant_builder_init(&models,G_VARIANT_TYPE("a(qa{sv})"));
            g_variant_builder_add(&models,"(q@a{sv})",1,dict());add("Models",g_variant_builder_end(&models));
            g_variant_builder_init(&models,G_VARIANT_TYPE("a(qqa{sv})"));g_variant_builder_add(&models,"(qq@a{sv})",0x1234,0x5679,dict());add("VendorModels",g_variant_builder_end(&models));
        } else if(i=="org.bluez.GattService1") {add("UUID",g_variant_new_string(uuid(0x00a0)));add("Primary",g_variant_new_boolean(true));}
        else if(i=="org.bluez.GattCharacteristic1") {
            auto id=characteristic(path);add("UUID",g_variant_new_string(uuid(id)));add("Service",g_variant_new_object_path(service));
            // const char* readNotify[]={"read","notify",nullptr};const char* readWrite[]={"read","write","authorize",nullptr};const char* read[]={"read",nullptr};const char* write[]={"write","authorize",nullptr};
            // Legacy phone control characteristics are retained below, but no longer exported.
            // add("Flags",g_variant_new_strv(id==0xa005||id==0xa00b?readNotify:id==0xa009?write:id==0xa00a?read:readWrite,-1));
            const char* credentials[]={"write","encrypt-write",nullptr};
            const char* commit[]={"read","write","encrypt-write","notify",nullptr};
            add("Flags",g_variant_new_strv(id==0xa00e?commit:credentials,-1));
            add("Value",array(value(id)));add("Notifying",g_variant_new_boolean(notifying[id]));
        } else if(i=="org.bluez.LEAdvertisement1") {add("Type",g_variant_new_string("peripheral"));const char* services[]={uuid(0x00a0),nullptr};add("ServiceUUIDs",g_variant_new_strv(services,-1));add("LocalName",g_variant_new_string("SRLAB_THPS_MAIN"));}
        return g_variant_builder_end(&b);
    }
    Bytes value(unsigned id) {
        // if(id==0xa005)return gateway.data();if(id==0xa00b)return gateway.status();if(id==0xa00a)return frozen;
        // auto v=gateway.view();return {id==0xa004?v.rotation:v.brightness};
        if(id!=0xa00e)return {};
        std::string state=wifiOnline?"connected":wifiConnecting?"connecting":!wifiError.empty()?"error":"idle";
        return Bytes(state.begin(),state.end());
    }
    void notify(unsigned id,bool force=false) {
        if(!notifying[id])return;auto b=value(id);if(id==0xa005&&b.empty())return;if(!force&&lastValue[id]==b)return;lastValue[id]=b;
        GVariantBuilder changes;g_variant_builder_init(&changes,G_VARIANT_TYPE_VARDICT);g_variant_builder_add(&changes,"{sv}","Value",array(b));
        g_dbus_connection_emit_signal(bus,nullptr,charPath(id).c_str(),"org.freedesktop.DBus.Properties","PropertiesChanged",g_variant_new("(s@a{sv}@as)","org.bluez.GattCharacteristic1",g_variant_builder_end(&changes),g_variant_new_strv(nullptr,0)),nullptr);
    }
    void attError(GDBusMethodInvocation* invocation,uint8_t code) {
        const char* name=code==6?"NotSupported":code==7?"InvalidOffset":code==0x0d?"InvalidValueLength":code==2||code==3?"NotPermitted":nullptr;
        if(name) {auto full=std::string("org.bluez.Error.")+name;g_dbus_method_invocation_return_dbus_error(invocation,full.c_str(),"Rejected");}
        else {char message[8];g_snprintf(message,sizeof(message),"0x%02x",code);g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Failed",message);}
    }
    void dispatch(const char* sender,const char* path,const char* iface,const char* method,GVariant* args,GDBusMethodInvocation* invocation) {
        bool mesh=std::string(iface).find("org.bluez.mesh.")==0;
        if((mesh&&sender!=meshOwner)||(!mesh&&sender!=phoneOwner&&std::strcmp(iface,"org.freedesktop.DBus.ObjectManager"))) {
            g_dbus_method_invocation_return_dbus_error(invocation,"org.freedesktop.DBus.Error.AccessDenied","Only the Bluetooth daemon may call this interface");return;
        }
        std::string m=method;
        if(m=="GetManagedObjects") {
            GVariantBuilder all;g_variant_builder_init(&all,G_VARIANT_TYPE("a{oa{sa{sv}}}"));
            std::map<std::string,std::vector<std::string>> grouped;for(const auto& o:objects)if(o.first.rfind(std::string(path)+"/",0)==0)grouped[o.first].push_back(o.second);
            for(const auto& object:grouped) {GVariantBuilder interfaces;g_variant_builder_init(&interfaces,G_VARIANT_TYPE("a{sa{sv}}"));for(const auto& interface:object.second)g_variant_builder_add(&interfaces,"{s@a{sv}}",interface.c_str(),properties(object.first.c_str(),interface.c_str()));g_variant_builder_add(&all,"{o@a{sa{sv}}}",object.first.c_str(),g_variant_builder_end(&interfaces));}
            g_dbus_method_invocation_return_value(invocation,g_variant_new("(@a{oa{sa{sv}}})",g_variant_builder_end(&all)));return;
        }
        if(m=="JoinComplete") {guint64 token;g_variant_get(args,"(t)",&token);gateway.joined(token);g_dbus_method_invocation_return_value(invocation,nullptr);attach();return;}
        if(m=="JoinFailed")throw std::runtime_error("BlueZ network creation failed; journal retained");
        if(m=="ScanResult") {auto b=byteChild(args,1);gateway.observed(b.data(),b.size());}
        else if(m=="RequestProvData") {guchar count;g_variant_get(args,"(y)",&count);auto address=gateway.reserve(count);g_dbus_method_invocation_return_value(invocation,g_variant_new("(qq)",0,address));return;}
        else if(m=="AddNodeComplete") {auto b=byteChild(args,0);guint16 address;guchar count;g_variant_get_child(args,1,"q",&address);g_variant_get_child(args,2,"y",&count);if(b.size()!=16)throw std::runtime_error("Invalid provisioned UUID");UUID u;std::copy(b.begin(),b.end(),u.begin());gateway.provisioned(u,address,count);}
        else if(m=="AddNodeFailed")gateway.provisionFailed();
        else if(m=="MessageReceived"||m=="DevKeyMessageReceived") {
            guint16 source,key;g_variant_get_child(args,0,"q",&source);g_variant_get_child(args,m=="MessageReceived"?1:2,"q",&key);auto b=byteChild(args,3);
            if(m=="DevKeyMessageReceived"&&source==1&&(addingLocalKey||localBinding||settingTtl)) {
                if(addingLocalKey&&b.size()==6&&b[0]==0x80&&b[1]==0x03) {
                    if(b[2])throw std::runtime_error("Cannot install local AppKey: status "+std::to_string(b[2]));
                    addingLocalKey=false;localBinding=true;send(1,{0x80,0x3d,1,0,0,0,0x34,0x12,0x79,0x56},true);
                }
                else if(localBinding&&b.size()==11&&b[0]==0x80&&b[1]==0x3e&&b[2]==0) {localBinding=false;settingTtl=true;send(1,{0x80,0x0d,7},true);}
                else if(settingTtl&&b==Bytes({0x80,0x0e,7})) {settingTtl=false;gateway.attached();std::cout<<"Mesh gateway attached; local model bound; TTL 7\n"<<std::flush;}
                else if(b.size()>=3&&b[0]==0x80&&b[1]==0x3e)throw std::runtime_error("Cannot bind local Mesh vendor model: status "+std::to_string(b[2]));
            } else gateway.message(source,key,b,m=="DevKeyMessageReceived");
        } else if(m=="ReadValue"||m=="WriteValue") {
            auto id=characteristic(path);bool write=m=="WriteValue";auto options=at(args,write?1:0);guint16 offset=0,mtu=23;const char* type="request";const char* device=nullptr;gboolean prepare=false;
            g_variant_lookup(options,"offset","q",&offset);g_variant_lookup(options,"mtu","q",&mtu);g_variant_lookup(options,"type","&s",&type);g_variant_lookup(options,"device","&o",&device);g_variant_lookup(options,"prepare-authorize","b",&prepare);
            std::string requestType=type,devicePath=device?device:"";g_variant_unref(options);
            if(!devicePath.empty()) {if(phone.empty())phone=devicePath;else if(phone!=devicePath) {attError(invocation,0x80);return;}}
            if(write) {
                auto b=byteChild(args,0);uint8_t error=0;
                if(prepare||requestType!="request")error=6;else if(offset)error=7;
                // else if(id==0xa009) {if(requestType!="request")error=6;else error=gateway.command(b,frozen);}
                // else if(id==0xa004||id==0xa008)error=b.size()!=1?0x0d:gateway.displayWrite(id==0xa004,b[0]);else error=3;
                else if(wifiOnline||wifiConnecting)error=MANAGEMENT_ATT_BUSY;
                else if(id==0xa00c) {if(!credentialFragment(ssid,b,32))error=0x0d;else ssidWritten=true;}
                else if(id==0xa00d) {if(!credentialFragment(password,b,64))error=0x0d;else passwordWritten=true;}
                else if(id==0xa00e) {
                    if(b!=Bytes{1}||!ssidWritten||!passwordWritten||!validWifiCredentials(ssid,password)||
                       !g_utf8_validate(ssid.data(),ssid.size(),nullptr)||!g_utf8_validate(password.data(),password.size(),nullptr))error=0xff;
                    else if(wifiDevice.empty())error=MANAGEMENT_ATT_NOT_READY;
                    else connectWifi();
                } else error=3;
                if(error) {attError(invocation,error);return;}
            } else {
                if(id!=0xa00e) {attError(invocation,2);return;}auto b=value(id);
                if(offset>b.size()) {attError(invocation,7);return;}size_t size=std::min(b.size()-offset,size_t(std::max<unsigned>(mtu,2)-1));
                Bytes slice(b.begin()+offset,b.begin()+offset+size);g_dbus_method_invocation_return_value(invocation,g_variant_new("(@ay)",array(slice)));return;
            }
        } else if(m=="StartNotify"||m=="StopNotify") {
            // Legacy notification IDs: A005 (data), A00B (management status).
            auto id=characteristic(path);if(id!=0xa00e) {attError(invocation,6);return;}notifying[id]=m=="StartNotify";
            g_dbus_method_invocation_return_value(invocation,nullptr);if(notifying[id])notify(id,true);return;
        } else if(m=="Release")advertised=false;
        else if(m!="UpdateModelConfiguration") {attError(invocation,6);return;}
        g_dbus_method_invocation_return_value(invocation,nullptr);
    }
    void add(const std::string& path,const char* interface) {
        static const GDBusInterfaceVTable table={
            [](GDBusConnection*,const gchar* sender,const gchar* path,const gchar* iface,const gchar* method,GVariant* args,GDBusMethodInvocation* invocation,gpointer pointer) {
                auto self=static_cast<Impl*>(pointer);try {self->dispatch(sender,path,iface,method,args,invocation);}catch(const std::exception& e){self->fail(e.what());g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Failed","0x0e");}
            },
            [](GDBusConnection*,const gchar*,const gchar* path,const gchar* iface,const gchar* key,GError**,gpointer pointer)->GVariant* {
                auto self=static_cast<Impl*>(pointer);auto p=g_variant_ref_sink(self->properties(path,iface));auto v=g_variant_lookup_value(p,key,nullptr);g_variant_unref(p);return v;
            },nullptr,{nullptr}};
        GError* error=nullptr;guint id=g_dbus_connection_register_object(bus,path.c_str(),g_dbus_node_info_lookup_interface(info,interface),&table,this,nullptr,&error);
        if(!id) {std::string message=error?error->message:"Cannot export D-Bus interface";g_clear_error(&error);throw std::runtime_error(message);}registrations.push_back(id);objects.emplace_back(path,interface);
    }
    void send(uint16_t destination,const Bytes& b,bool deviceKey) {
        if(destination==1) {sendNow(destination,b,deviceKey);return;}
        // Legacy advertising instances share one controller. Keep phone
        // advertising out of the Mesh TX slot, including REPORT acknowledgments.
        outgoing.push_back({destination,b,deviceKey});
        advertiseAfter=monotonicUs()+500000;
        stopAdvertising();
    }
    void flushOutgoing() {
        if(!gateway.ready||registering||advertisingStopping||advertised)return;
        while(!outgoing.empty()) {
            auto message=std::move(outgoing.front());outgoing.pop_front();
            sendNow(message.destination,message.bytes,message.deviceKey);
            advertiseAfter=monotonicUs()+500000;
        }
    }
    void sendNow(uint16_t destination,const Bytes& b,bool deviceKey) {
        if(deviceKey&&b==Bytes({0,0,0,0})) {
            call("org.bluez.mesh",nodePath,"org.bluez.mesh.Node1","AddAppKey",g_variant_new("(oqqqb)",element,destination,0,0,false));return;
        }
        // BlueZ requires remote-device-key addressing even for local loopback.
        auto args=deviceKey?g_variant_new("(oqbq@a{sv}@ay)",element,destination,true,0,dict(),array(b)):g_variant_new("(oqq@a{sv}@ay)",element,destination,0,dict(),array(b));
        call("org.bluez.mesh",nodePath,"org.bluez.mesh.Node1",deviceKey?"DevKeySend":"Send",args);
    }
    void attach() {
        call("org.bluez.mesh",network,"org.bluez.mesh.Network1","Attach",g_variant_new("(ot)",meshRoot,gateway.token),[this](GVariant* reply,const GError* error) {
            if(error)throw std::runtime_error(std::string("Attach failed; network retained: ")+error->message);
            const char* path;g_variant_get_child(reply,0,"&o",&path);nodePath=path;
            meshCall("CreateAppKey",g_variant_new("(qq)",0,0),[this](GVariant*,const GError* error) {
                if(error&&!g_dbus_error_is_remote_error(error))throw std::runtime_error(error->message);
                if(error) {auto name=g_dbus_error_get_remote_error(error);bool exists=name&&std::string(name)=="org.bluez.mesh.Error.AlreadyExists";g_free(name);if(!exists)throw std::runtime_error(error->message);}
                addingLocalKey=true;localDeadline=monotonicUs()+10000000;send(1,{0,0,0,0},true);
            });
        });
    }
    void advertise() {
        if(wifiOnline||!phoneRegistered||phonePaused||advertised||registering||advertisingStopping||!phone.empty()||!outgoing.empty()||monotonicUs()<advertiseAfter)return;registering=true;
        call("org.bluez",adapter,"org.bluez.LEAdvertisingManager1","RegisterAdvertisement",g_variant_new("(o@a{sv})",advertisement,dict()),[this](GVariant*,const GError* error){registering=false;if(error)throw std::runtime_error(error->message);advertised=true;if(phonePaused||!phone.empty()||!outgoing.empty()||monotonicUs()<advertiseAfter)stopAdvertising();});
    }
    void registerPhone() {
        if(wifiOnline||phonePaused||phoneRegistered||phoneRegistering||phoneUnregistering)return;
        phoneRegistering=true;
        call("org.bluez",adapter,"org.bluez.GattManager1","RegisterApplication",g_variant_new("(o@a{sv})",phoneRoot,dict()),[this](GVariant*,const GError* error){phoneRegistering=false;if(error)throw std::runtime_error(error->message);phoneRegistered=true;advertise();});
    }
    void stopAdvertising() {
        if(!advertised||advertisingStopping)return;advertisingStopping=true;
        call("org.bluez",adapter,"org.bluez.LEAdvertisingManager1","UnregisterAdvertisement",g_variant_new("(o)",advertisement),[this](GVariant*,const GError* error){advertisingStopping=false;if(error)throw std::runtime_error(error->message);advertised=false;});
    }
    void phoneDisconnected() {
        phone.clear();frozen=Bytes(8);frozen[0]=1;notifying.clear();lastValue.clear();
        ssid.clear();password.clear();ssidWritten=passwordWritten=false;
        // gateway.disconnect(); Phone disconnect must not cancel discovery started from the web.
        if(!phonePaused)advertise();
    }
    void disconnectPhone() {
        if(phone.empty()||phoneDisconnecting)return;
        phoneDisconnecting=true;auto device=phone;
        call("org.bluez",device,"org.bluez.Device1","Disconnect",nullptr,[this,device](GVariant*,const GError* error){
            phoneDisconnecting=false;
            if(error) {
                auto name=g_dbus_error_get_remote_error(error);
                bool gone=name&&(!strcmp(name,"org.bluez.Error.NotConnected")||!strcmp(name,"org.freedesktop.DBus.Error.UnknownObject"));g_free(name);
                if(!gone)throw std::runtime_error(error->message);
            }
            if(phone==device)phoneDisconnected();
        });
    }
    bool prepareConfiguration() {
        if(!gateway.ready)return false;
        auto now=monotonicUs();
        if(!phonePaused) {
            phonePaused=true;freshMeshSession=false;
            // Let bluetoothd return the accepted ATT write before disconnecting.
            phonePauseAfter=now+250000;phonePauseDeadline=now+8000000;
            notify(0xa00b,true);
            std::cout<<"Pausing phone GATT for node provisioning/configuration\n"<<std::flush;
            return false;
        }
        if(now<phonePauseAfter)return false;
        // Subsequent nodes can reuse the completed handoff, even after 8 s.
        if(phone.empty()&&!phoneDisconnecting&&!registering&&!advertised&&!advertisingStopping&&
           !phoneRegistering&&!phoneRegistered&&!phoneUnregistering) {
            if(!freshMeshSession) {
                // A fresh owner drains the previous local Mesh session, just
                // as in the successful standalone provisioning operation.
                reopeningMesh=true;
                std::cout<<"Refreshing Mesh session before node operation\n"<<std::flush;
                return false;
            }
            return true;
        }
        if(now>=phonePauseDeadline)throw std::runtime_error("Phone GATT pause timed out; node operation was not started");
        stopAdvertising();disconnectPhone();
        if(!phone.empty()||phoneDisconnecting||registering||advertised||advertisingStopping||phoneRegistering)return false;
        if(phoneRegistered&&!phoneUnregistering) {
            phoneUnregistering=true;
            call("org.bluez",adapter,"org.bluez.GattManager1","UnregisterApplication",g_variant_new("(o)",phoneRoot),[this](GVariant*,const GError* error){
                phoneUnregistering=false;if(error)throw std::runtime_error(error->message);phoneRegistered=false;
            });
        }
        return false;
    }
    void resumePhone() {
        phonePaused=false;
        std::cout<<"Node operation finished; restoring phone GATT\n"<<std::flush;
        registerPhone();
    }
    void syncPhone() {
        if(!wifiOnline) {if(!phonePaused)registerPhone();return;}
        stopAdvertising();disconnectPhone();
        if(phone.empty()&&!phoneDisconnecting&&!registering&&!advertised&&!advertisingStopping&&!phoneRegistering&&phoneRegistered&&!phoneUnregistering) {
            phoneUnregistering=true;
            call("org.bluez",adapter,"org.bluez.GattManager1","UnregisterApplication",g_variant_new("(o)",phoneRoot),[this](GVariant*,const GError* error){phoneUnregistering=false;if(error)throw std::runtime_error(error->message);phoneRegistered=false;});
        }
    }
    void wifiCall(const std::string& path,const char* iface,const char* method,GVariant* args,Done done) {
        call("org.freedesktop.NetworkManager",path,iface,method,args,std::move(done));
    }
    void pollWifi() {
        auto now=monotonicUs();
        if(wifiConnecting&&now>=wifiConnectDeadline) {wifiConnecting=false;wifiError="Wi-Fi connection timed out";}
        if(wifiPolling||now<wifiPollAfter)return;
        wifiPolling=true;wifiPollAfter=now+1000000;
        wifiCall("/org/freedesktop/NetworkManager","org.freedesktop.NetworkManager","GetDeviceByIpIface",g_variant_new("(s)","wlan0"),[this](GVariant* reply,const GError* error){
            if(error) {wifiPolling=false;wifiOnline=false;wifiDevice.clear();wifiIp.clear();gateway.networkAddress("");wifiError="NetworkManager/wlan0 unavailable";return;}
            const char* path;g_variant_get(reply,"(&o)",&path);wifiDevice=path;
            wifiCall(wifiDevice,"org.freedesktop.DBus.Properties","GetAll",g_variant_new("(s)","org.freedesktop.NetworkManager.Device"),[this](GVariant* reply,const GError* error){
                wifiPolling=false;guint32 state=0;
                if(!error) {auto properties=at(reply,0);g_variant_lookup(properties,"State","u",&state);g_variant_unref(properties);}
                wifiIp.clear();
                if(state==100) {
                    ifaddrs* interfaces=nullptr;
                    if(getifaddrs(&interfaces)==0) {
                        for(auto p=interfaces;p;p=p->ifa_next)if(p->ifa_addr&&std::string(p->ifa_name)=="wlan0"&&p->ifa_addr->sa_family==AF_INET) {
                            char address[INET_ADDRSTRLEN];auto ipv4=reinterpret_cast<sockaddr_in*>(p->ifa_addr);
                            if(inet_ntop(AF_INET,&ipv4->sin_addr,address,sizeof(address))&&std::string(address).rfind("169.254.",0)!=0) {wifiIp=address;break;}
                        }
                        freeifaddrs(interfaces);
                    }
                }
                wifiOnline=!wifiIp.empty();gateway.networkAddress(wifiIp);
                if(wifiOnline) {
                    wifiPolling=true;
                    wifiCall(wifiDevice,"org.freedesktop.DBus.Properties","GetAll",g_variant_new("(s)","org.freedesktop.NetworkManager.Device.Wireless"),[this](GVariant* reply,const GError* error){
                        std::string accessPoint;
                        if(!error) {auto properties=at(reply,0);const char* path=nullptr;if(g_variant_lookup(properties,"ActiveAccessPoint","&o",&path))accessPoint=path;g_variant_unref(properties);}
                        if(accessPoint.empty()||accessPoint=="/") {wifiPolling=false;return;}
                        wifiCall(accessPoint,"org.freedesktop.DBus.Properties","GetAll",g_variant_new("(s)","org.freedesktop.NetworkManager.AccessPoint"),[this](GVariant* reply,const GError* error){
                            wifiPolling=false;std::string name;
                            if(!error) {auto properties=at(reply,0);auto ssid=g_variant_lookup_value(properties,"Ssid",G_VARIANT_TYPE("ay"));if(ssid) {auto data=bytes(ssid);name.assign(data.begin(),data.end());g_variant_unref(ssid);}g_variant_unref(properties);}
                            gateway.networkAddress(wifiIp,name);
                        });
                    });
                }
                if(wifiOnline) {wifiConnecting=false;wifiError.clear();}
                else if(wifiConnecting&&state==120) {wifiConnecting=false;wifiError="Wi-Fi authentication or connection failed";}
                else if(error)wifiError="Cannot read Wi-Fi status";
                notify(0xa00e);
            });
        });
    }
    void connectWifi() {
        wifiConnecting=true;wifiError.clear();wifiConnectDeadline=monotonicUs()+60000000;
        GVariantBuilder settings,section;g_variant_builder_init(&settings,G_VARIANT_TYPE("a{sa{sv}}"));
        auto begin=[&]{g_variant_builder_init(&section,G_VARIANT_TYPE_VARDICT);};
        auto add=[&](const char* key,GVariant* value){g_variant_builder_add(&section,"{sv}",key,value);};
        auto end=[&](const char* name){g_variant_builder_add(&settings,"{s@a{sv}}",name,g_variant_builder_end(&section));};
        constexpr const char* profileUuid="2d908d6e-5d93-4d28-b02f-cd47341861ac";
        begin();add("id",g_variant_new_string("smartrabbit-wifi"));add("uuid",g_variant_new_string(profileUuid));add("type",g_variant_new_string("802-11-wireless"));add("interface-name",g_variant_new_string("wlan0"));add("autoconnect",g_variant_new_boolean(true));end("connection");
        begin();add("ssid",array(Bytes(ssid.begin(),ssid.end())));add("mode",g_variant_new_string("infrastructure"));end("802-11-wireless");
        if(!password.empty()) {begin();add("key-mgmt",g_variant_new_string("wpa-psk"));add("psk",g_variant_new_string(password.c_str()));end("802-11-wireless-security");}
        begin();add("method",g_variant_new_string("auto"));end("ipv4");
        begin();add("method",g_variant_new_string("auto"));end("ipv6");
        auto config=std::shared_ptr<GVariant>(g_variant_ref_sink(g_variant_builder_end(&settings)),g_variant_unref);
        password.clear();ssid.clear();ssidWritten=passwordWritten=false;
        auto failed=[this]{wifiConnecting=false;wifiError="NetworkManager rejected Wi-Fi credentials or profile";notify(0xa00e);};
        // Update only our fixed-UUID profile; other Wi-Fi/Netplan settings are untouched.
        wifiCall("/org/freedesktop/NetworkManager/Settings","org.freedesktop.NetworkManager.Settings","GetConnectionByUuid",g_variant_new("(s)",profileUuid),[this,config,failed](GVariant* reply,const GError* error){
            if(error) {
                wifiCall("/org/freedesktop/NetworkManager","org.freedesktop.NetworkManager","AddAndActivateConnection",g_variant_new("(@a{sa{sv}}oo)",g_variant_ref(config.get()),wifiDevice.c_str(),"/"),[this,failed](GVariant*,const GError* error){if(error)failed();else wifiPollAfter=0;});
            } else {
                const char* path;g_variant_get(reply,"(&o)",&path);std::string profile=path;
                wifiCall(profile,"org.freedesktop.NetworkManager.Settings.Connection","Update",g_variant_new("(@a{sa{sv}})",g_variant_ref(config.get())),[this,profile,failed](GVariant*,const GError* error){
                    if(error) {failed();return;}
                    wifiCall("/org/freedesktop/NetworkManager","org.freedesktop.NetworkManager","ActivateConnection",g_variant_new("(ooo)",profile.c_str(),wifiDevice.c_str(),"/"),[this,failed](GVariant*,const GError* error){if(error)failed();else wifiPollAfter=0;});
                });
            }
        });
    }
    WebReply handleWeb(const std::string& path,const std::string& body) {
        if(path=="/api/state") {
            auto json=gateway.webState();json.pop_back();
            return {200,json+",\"wifi\":{\"connected\":"+(wifiOnline?"true":"false")+",\"ip\":"+jsonString(wifiIp)+",\"error\":"+jsonString(wifiError)+"}}"};
        }
        auto fields=decodeForm(body);
        auto number=[&](const char* key,unsigned min,unsigned max) {
            auto it=fields.find(key);if(it==fields.end()||it->second.empty()||it->second.size()>5||it->second.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid numeric field");
            unsigned value=std::stoul(it->second);if(value<min||value>max)throw std::runtime_error("Field out of range");return value;
        };
        uint8_t error=0;
        if(path=="/api/menu") {
            unsigned count=number("count",0,16);if(fields.size()!=1+count*3)return {400,"{}"};
            std::vector<MenuEntry> entries;
            for(unsigned i=0;i<count;++i) {
                const auto prefix=std::to_string(i);auto name=fields.find("name"+prefix);if(name==fields.end())return {400,"{}"};
                entries.push_back({number(("type"+prefix).c_str(),0,5),name->second,uint16_t(number(("address"+prefix).c_str(),0,32767))});
            }
            error=gateway.menuWrite(entries);
        } else if(path=="/api/display") {
            if(fields.size()!=1)return {400,"{\"error\":\"Write one display setting at a time\"}"};
            bool rotation=fields.count("rotation");error=gateway.displayWrite(rotation,uint8_t(number(rotation?"rotation":"brightness",rotation?1:0,rotation?4:100)));
        } else {
            auto id=number("id",1,65535);Bytes command{1,0,uint8_t(id),uint8_t(id>>8)},snapshot;
            if(path=="/api/scan") {command[1]=MANAGEMENT_SCAN_START;command.push_back(15);}
            else if(path=="/api/bind") {
                command[1]=MANAGEMENT_BIND;auto uuid=fields.find("uuid");if(uuid==fields.end()||uuid->second.size()!=32||uuid->second.find_first_not_of("0123456789abcdef")!=std::string::npos)return {400,"{\"error\":\"Invalid UUID\"}"};
                for(size_t i=0;i<32;i+=2)command.push_back(uint8_t(std::stoul(uuid->second.substr(i,2),nullptr,16)));
            } else if(path=="/api/remove"||path=="/api/forget")error=gateway.webRemove(uint16_t(number("address",1,32767)),uint16_t(id),path=="/api/forget");
            else if(path=="/api/retry") {command[1]=MANAGEMENT_RETRY_CONFIG;auto address=number("address",1,32767);command.push_back(uint8_t(address));command.push_back(uint8_t(address>>8));}
            else return {404,"{}"};
            if(path!="/api/remove"&&path!="/api/forget")error=gateway.command(command,snapshot);
        }
        return error?WebReply{409,"{\"error\":\"Command rejected\",\"code\":"+std::to_string(error)+"}"}:WebReply{200,"{\"accepted\":true}"};
    }
    WebReply webRequest(const std::string& path,const std::string& body) {
        auto request=std::make_shared<WebRequest>();request->path=path;request->body=body;auto result=request->result.get_future();
        {std::lock_guard<std::mutex> lock(webMutex);if(stopping)return {503,"{\"error\":\"Gateway unavailable\"}"};webRequests.push_back(request);}
        if(result.wait_for(std::chrono::seconds(2))!=std::future_status::ready)return {503,"{\"error\":\"Gateway busy; refresh status before retrying\"}"};
        return result.get();
    }
    void processWeb() {
        const int level=requestedBrightness.exchange(-1);
        if(level>=0)gateway.displayWrite(false,uint8_t(level));
        std::deque<std::shared_ptr<WebRequest>> requests;
        {std::lock_guard<std::mutex> lock(webMutex);requests.swap(webRequests);}
        for(auto& request:requests)try {request->result.set_value(handleWeb(request->path,request->body));}
        catch(const std::exception&) {request->result.set_value({400,"{\"error\":\"Invalid request\"}"});}
    }
    std::string owner(const char* name) {
        GError* error=nullptr;auto reply=g_dbus_connection_call_sync(bus,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetNameOwner",g_variant_new("(s)",name),G_VARIANT_TYPE("(s)"),G_DBUS_CALL_FLAGS_NONE,5000,nullptr,&error);
        if(!reply) {std::string message=error?error->message:"Missing Bluetooth service";g_clear_error(&error);throw std::runtime_error(message);}const char* result;g_variant_get(reply,"(&s)",&result);std::string answer=result;g_variant_unref(reply);return answer;
    }
    void run() {
        do {
            if(reopeningMesh) {
                gateway.ready=false;freshMeshSession=true;
                addingLocalKey=localBinding=settingTtl=false;localDeadline=0;
                phonePauseDeadline=monotonicUs()+8000000;
            }
            reopeningMesh=false;
            runSession();
        } while(reopeningMesh&&!stopping);
    }
    void runSession() {
        context=g_main_context_new();g_main_context_push_thread_default(context);cancel=g_cancellable_new();
        try {
            GError* error=nullptr;auto address=g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM,nullptr,&error);
            if(address) {bus=g_dbus_connection_new_for_address_sync(address,GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),nullptr,nullptr,&error);g_free(address);}
            if(!bus) {std::string message=error?error->message:"No system bus";g_clear_error(&error);throw std::runtime_error(message);}
            g_dbus_connection_set_exit_on_close(bus,false);
            meshOwner=owner("org.bluez.mesh");phoneOwner=owner("org.bluez");info=g_dbus_node_info_new_for_xml(xml,&error);if(!info)throw std::runtime_error(error->message);
            add(meshRoot,"org.freedesktop.DBus.ObjectManager");add(meshApp,"org.bluez.mesh.Application1");add(meshApp,"org.bluez.mesh.Provisioner1");add(std::string(meshRoot)+"/agent","org.bluez.mesh.ProvisionAgent1");add(element,"org.bluez.mesh.Element1");
            add(phoneRoot,"org.freedesktop.DBus.ObjectManager");add(service,"org.bluez.GattService1");
            // Legacy phone characteristics retained, intentionally disabled:
            // for(auto id:{0xa005,0xa004,0xa008,0xa009,0xa00a,0xa00b})add(charPath(id),"org.bluez.GattCharacteristic1");
            for(auto id:{0xa00c,0xa00d,0xa00e})add(charPath(id),"org.bluez.GattCharacteristic1");
            add(advertisement,"org.bluez.LEAdvertisement1");
            gateway.send=[this](uint16_t destination,const Bytes& b,bool device){send(destination,b,device);};
            gateway.bind=[this](const UUID& u){meshCall("AddNode",g_variant_new("(@ay@a{sv})",array(Bytes(u.begin(),u.end())),dict()),[](GVariant*,const GError* error){if(error)throw std::runtime_error(std::string("AddNode not confirmed; provisioning journal retained: ")+error->message);});};
            gateway.erase=[this](uint16_t a,uint8_t count){meshCall("DeleteRemoteNode",g_variant_new("(qy)",a,count),[this,a](GVariant*,const GError* error){gateway.deleted(a,!error);});};
            gateway.scan=[this](bool on,unsigned seconds){
                if(on) {GVariantBuilder options;g_variant_builder_init(&options,G_VARIANT_TYPE_VARDICT);g_variant_builder_add(&options,"{sv}","Seconds",g_variant_new_uint16(uint16_t(seconds)));meshCall("UnprovisionedScan",g_variant_new("(@a{sv})",g_variant_builder_end(&options)));}
                else meshCall("UnprovisionedScanCancel",nullptr);
            };
            gateway.changed=[this](uint16_t id){notify(id);};
            gateway.configurationReady=[this]{return prepareConfiguration();};
            subscriptions.push_back(g_dbus_connection_signal_subscribe(bus,"org.bluez","org.freedesktop.DBus.Properties","PropertiesChanged",nullptr,nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
                [](GDBusConnection*,const gchar*,const gchar* path,const gchar*,const gchar*,GVariant* args,gpointer pointer) {
                    auto self=static_cast<Impl*>(pointer);const char* iface;GVariant* properties;GVariant* invalidated;g_variant_get(args,"(&s@a{sv}@as)",&iface,&properties,&invalidated);gboolean connected;
                    if(!strcmp(iface,"org.bluez.Device1")&&std::string(path).rfind(self->adapter+"/",0)==0&&g_variant_lookup(properties,"Connected","b",&connected))try {
                        if(connected) {if(self->phone.empty()) {self->phone=path;self->stopAdvertising();if(self->phonePaused||self->wifiOnline)self->disconnectPhone();}else if(self->phone!=path)self->call("org.bluez",path,"org.bluez.Device1","Disconnect",nullptr);}
                        else if(self->phone==path)self->phoneDisconnected();
                    }catch(const std::exception& e){self->fail(e.what());}g_variant_unref(properties);g_variant_unref(invalidated);
                },this,nullptr));
            subscriptions.push_back(g_dbus_connection_signal_subscribe(bus,"org.freedesktop.DBus","org.freedesktop.DBus","NameOwnerChanged","/org/freedesktop/DBus",nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
                [](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* args,gpointer pointer){auto self=static_cast<Impl*>(pointer);const char* name;const char* old;const char* next;g_variant_get(args,"(&s&s&s)",&name,&old,&next);if(!strcmp(name,"org.bluez")||!strcmp(name,"org.bluez.mesh"))self->fail("Bluetooth daemon changed; restart gateway to reattach safely");},this,nullptr));
            wifiPolling=false;wifiPollAfter=0;
            if(gateway.creating)call("org.bluez.mesh",network,"org.bluez.mesh.Network1","CreateNetwork",g_variant_new("(o@ay)",meshRoot,array(Bytes(gateway.identity.begin(),gateway.identity.end()))));else attach();
            while(!stopping&&!reopeningMesh) {
                for(unsigned i=0;i<64&&g_main_context_pending(context);++i)g_main_context_iteration(context,false);
                if(g_dbus_connection_is_closed(bus))throw std::runtime_error("System bus disconnected");
                if((addingLocalKey||localBinding||settingTtl)&&monotonicUs()>=localDeadline)throw std::runtime_error("Local Mesh key/binding/TTL configuration timed out");
                flushOutgoing();
                pollWifi();processWeb();
                gateway.tick();
                flushOutgoing();
                if(phonePaused&&gateway.ready&&!reopeningMesh&&!gateway.configurationPending())resumePhone();
                syncPhone();
                if(phoneRegistered)advertise();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } catch(const std::exception& e) {fail(e.what());}
        g_cancellable_cancel(cancel);while(pending)g_main_context_iteration(context,true);
        if(bus) {
            for(auto id:subscriptions)g_dbus_connection_signal_unsubscribe(bus,id);
            for(auto id:registrations)g_dbus_connection_unregister_object(bus,id);
            g_dbus_connection_close_sync(bus,nullptr,nullptr);g_object_unref(bus);bus=nullptr;
        }
        if(info)g_dbus_node_info_unref(info);g_object_unref(cancel);g_main_context_pop_thread_default(context);g_main_context_unref(context);
        info=nullptr;cancel=nullptr;context=nullptr;subscriptions.clear();registrations.clear();objects.clear();
    }
};
BluezGateway::BluezGateway(const std::string& directory,const std::string& adapter,bool create):impl(std::make_unique<Impl>(directory,adapter,create)){}
BluezGateway::~BluezGateway()=default;
View BluezGateway::view() const {return impl->gateway.view();}
void BluezGateway::requestBrightness(int percent) {impl->requestedBrightness=std::clamp(percent,0,100);}
WebReply BluezGateway::webRequest(const std::string& path,const std::string& body) {return impl->webRequest(path,body);}
}
