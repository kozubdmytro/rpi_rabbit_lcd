#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sr {
// Each ATT write starts with the byte offset, followed by at most 19 data bytes
// at MTU 23. Offset zero replaces the field; other offsets must append exactly.
inline bool credentialFragment(std::string& field,const std::vector<uint8_t>& bytes,size_t limit) {
    if(bytes.empty())return false;
    size_t offset=bytes[0],length=bytes.size()-1;
    if((offset&&offset!=field.size())||offset+length>limit)return false;
    if(!offset)field.clear();
    field.append(reinterpret_cast<const char*>(bytes.data()+1),length);return true;
}
inline bool validWifiCredentials(const std::string& ssid,const std::string& password) {
    if(ssid.empty()||ssid.size()>32||ssid.find('\0')!=std::string::npos||password.find('\0')!=std::string::npos)return false;
    if(password.empty())return true;
    if(password.size()==64)return password.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos;
    return password.size()>=8&&password.size()<=63;
}
}
