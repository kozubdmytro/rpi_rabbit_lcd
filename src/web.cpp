#include "web.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace sr {
std::string jsonString(const std::string& value) {
    std::string out="\"";
    constexpr char digits[]="0123456789abcdef";
    for(unsigned char ch:value) {
        if(ch=='"'||ch=='\\') {out+='\\';out+=char(ch);}
        else if(ch<32) {out+="\\u00";out+=digits[ch>>4];out+=digits[ch&15];}
        else out+=char(ch);
    }
    return out+'"';
}
std::map<std::string,std::string> decodeForm(const std::string& body) {
    auto decode=[](const std::string& value) {
        std::string out;
        auto digit=[](char ch)->int {if(ch>='0'&&ch<='9')return ch-'0';if(ch>='a'&&ch<='f')return ch-'a'+10;if(ch>='A'&&ch<='F')return ch-'A'+10;return -1;};
        for(size_t i=0;i<value.size();++i) {
            char ch=value[i];
            if(ch=='%') {if(i+2>=value.size()||digit(value[i+1])<0||digit(value[i+2])<0)throw std::runtime_error("Invalid form encoding");ch=char(digit(value[i+1])*16+digit(value[i+2]));i+=2;}
            else if(ch=='+')ch=' ';
            out+=ch;
        }
        return out;
    };
    std::map<std::string,std::string> out;
    for(size_t start=0;start<body.size();) {
        auto end=body.find('&',start);if(end==std::string::npos)end=body.size();
        auto equal=body.find('=',start);if(equal==std::string::npos||equal>=end)throw std::runtime_error("Invalid form");
        if(!out.emplace(decode(body.substr(start,equal-start)),decode(body.substr(equal+1,end-equal-1))).second)throw std::runtime_error("Duplicate form field");
        start=end+1;
    }
    return out;
}
WebServer::WebServer(unsigned port,const std::string& directory,Handler handler) {
    if(!port||port>65535)throw std::runtime_error("Invalid web port");
    std::map<std::string,WebReply> assets;
    for(const auto& file:std::map<std::string,std::string>{{"index.html","text/html; charset=utf-8"},{"app.js","text/javascript; charset=utf-8"},{"style.css","text/css; charset=utf-8"}}) {
        std::ifstream stream(directory+"/"+file.first,std::ios::binary);
        if(!stream)throw std::runtime_error("Missing web asset: "+directory+"/"+file.first);
        std::ostringstream content;content<<stream.rdbuf();assets.emplace("/"+file.first,WebReply{200,content.str(),file.second});
    }
    assets.emplace("/",assets.at("/index.html"));
    listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(listener<0)throw std::runtime_error("Cannot create web listener");
    int reuse=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_ANY);address.sin_port=htons(uint16_t(port));
    if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0||listen(listener,8)<0) {close(listener);listener=-1;throw std::runtime_error("Cannot listen on web port "+std::to_string(port));}
    try {
        worker=std::thread([this,assets=std::move(assets),handler=std::move(handler)] {
            while(!stopping) {
                pollfd ready{listener,POLLIN,0};if(poll(&ready,1,200)<=0)continue;
                int client=accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);if(client<0)continue;
                try {serve(client,assets,handler);}catch(...) {/* A failed client must not stop Mesh. */}
                close(client);
            }
        });
    } catch(...) {close(listener);listener=-1;throw;}
}
WebServer::~WebServer() {stopping=true;if(worker.joinable())worker.join();if(listener>=0)close(listener);}
void WebServer::serve(int client,const std::map<std::string,WebReply>& assets,const Handler& handler) {
    timeval timeout{2,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    // Bound the whole request as well as individual reads (slow clients cannot pin the server).
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    auto read=[&](std::string& data) {
        if(stopping||std::chrono::steady_clock::now()>=deadline)return false;
        char buffer[1024];auto count=recv(client,buffer,sizeof(buffer),0);if(count<=0)return false;data.append(buffer,size_t(count));return true;
    };
    WebReply reply;
    try {
        std::string data;size_t split;
        while((split=data.find("\r\n\r\n"))==std::string::npos) {if(data.size()>8192||!read(data))return;}
        if(split>8192)throw std::runtime_error("Headers too large");
        std::istringstream headers(data.substr(0,split));std::string method,path,version,line;
        if(!(headers>>method>>path>>version)||version!="HTTP/1.1")throw std::runtime_error("Invalid HTTP request");
        std::getline(headers,line);std::map<std::string,std::string> fields;
        while(std::getline(headers,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();auto colon=line.find(':');if(colon==std::string::npos)throw std::runtime_error("Invalid header");
            auto key=line.substr(0,colon);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char ch){return char(std::tolower(ch));});
            auto value=line.substr(colon+1);auto start=value.find_first_not_of(" \t");value=start==std::string::npos?"":value.substr(start);
            if(!fields.emplace(key,value).second)throw std::runtime_error("Duplicate header");
        }
        if(fields.count("transfer-encoding"))throw std::runtime_error("Chunked requests are not supported");
        size_t length=0;
        if(fields.count("content-length")) {const auto& value=fields.at("content-length");if(value.empty()||value.size()>4||value.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid body length");length=std::stoul(value);}
        if(length>2048)throw std::runtime_error("Body too large");
        std::string body=data.substr(split+4);
        while(body.size()<length)if(!read(body))return;
        if(body.size()!=length)throw std::runtime_error("Invalid body length");
        if(method=="GET"&&path=="/api/state")reply=handler(path,"");
        else if(method=="GET") {auto found=assets.find(path);reply=found==assets.end()?WebReply{404,"{}"}:found->second;}
        else if(method=="POST"&&path.rfind("/api/",0)==0) {
            // Browser cross-origin requests need a preflight for this header; OPTIONS is refused.
            if(fields["x-sr-request"]!="1"||fields["content-type"]!="application/x-www-form-urlencoded")reply={403,"{\"error\":\"Same-origin request required\"}"};
            else reply=handler(path,body);
        } else reply={405,"{\"error\":\"Method not allowed\"}"};
    } catch(const std::exception&) {reply={400,"{\"error\":\"Invalid HTTP request\"}"};}
    std::string response="HTTP/1.1 "+std::to_string(reply.status)+" "+(reply.status<400?"OK":"Error")+"\r\nContent-Type: "+reply.contentType+"\r\nContent-Length: "+std::to_string(reply.body.size())+"\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; object-src 'none'; frame-ancestors 'none'\r\n\r\n"+reply.body;
    for(size_t offset=0;offset<response.size();) {auto sent=send(client,response.data()+offset,response.size()-offset,MSG_NOSIGNAL);if(sent<0&&errno==EINTR)continue;if(sent<=0)return;offset+=size_t(sent);}
}
}
