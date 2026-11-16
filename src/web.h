#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace sr {
struct WebReply { int status=200; std::string body,contentType="application/json"; };
std::string jsonString(const std::string& value);
std::map<std::string,std::string> decodeForm(const std::string& body);
class WebServer {
public:
    using Handler=std::function<WebReply(const std::string&,const std::string&)>;
    WebServer(unsigned port,const std::string& directory,Handler handler);
    ~WebServer();
    WebServer(const WebServer&)=delete;
    WebServer& operator=(const WebServer&)=delete;
private:
    int listener=-1;
    std::atomic<bool> stopping{false};
    std::thread worker;
    void serve(int client,const std::map<std::string,WebReply>& assets,const Handler& handler);
};
}
