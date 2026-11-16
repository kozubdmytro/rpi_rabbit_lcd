#pragma once
#include "gateway.h"
#include "../web.h"
#include <atomic>
#include <memory>
#include <thread>
namespace sr::mesh {
class BluezGateway {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    BluezGateway(const std::string& directory,const std::string& phoneAdapter,bool createNetwork);
    ~BluezGateway();
    BluezGateway(const BluezGateway&)=delete;
    BluezGateway& operator=(const BluezGateway&)=delete;
    View view() const;
    void requestBrightness(int percent);
    WebReply webRequest(const std::string& path,const std::string& body);
};
}
