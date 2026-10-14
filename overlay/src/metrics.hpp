#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace fps {
inline std::vector<std::string> csv(const std::string& text) {
    std::vector<std::string> fields; std::string field; bool quote = false;
    for (size_t i=0;i<text.size();++i) {
        char c=text[i];
        if(c=='"') { if(quote && i+1<text.size() && text[i+1]=='"') {field+='"';++i;} else quote=!quote; }
        else if(c==',' && !quote) {fields.push_back(field);field.clear();}
        else if(c!='\r') field+=c;
    }
    if(quote) return {}; fields.push_back(field); return fields;
}
struct Reading { double value=0; size_t samples=0; std::string chain; uint64_t age=0; bool valid=false; };
struct Stream {std::deque<double> intervals; double total=0; uint64_t last=0;};
class Metrics {
    int interval=-1,chain=-1,pid=-1; std::map<std::string,Stream> streams;
public:
    uint64_t rows=0; bool header=false;
    bool line(const std::string& text,uint32_t target,uint64_t now) {
        auto f=csv(text); if(f.empty()) return false;
        if(f[0]=="Application") {interval=chain=pid=-1;for(size_t i=0;i<f.size();++i){if(f[i]=="MsBetweenPresents")interval=(int)i;else if(f[i]=="SwapChainAddress")chain=(int)i;else if(f[i]=="ProcessID")pid=(int)i;}header=interval>=0&&chain>=0&&pid>=0;return false;}
        if(!header || std::max({interval,chain,pid})>=(int)f.size()) return false;
        try {
            size_t end=0; auto process=std::stoul(f[pid],&end);if(end!=f[pid].size()||process!=target)return false;
            double ms=std::stod(f[interval],&end); if(end!=f[interval].size()||!std::isfinite(ms)||ms<=0||ms>30000)return false;
            auto& s=streams[f[chain]];s.last=now;s.intervals.push_back(ms);s.total+=ms;rows++;
            while(s.intervals.size()>1 && (s.total-s.intervals.front()>=1000 || s.intervals.size()>5000)){s.total-=s.intervals.front();s.intervals.pop_front();}
            return true;
        } catch(...) {return false;}
    }
    Reading current(uint64_t now) const {
        Reading r;
        for(const auto& [name,s]:streams){if(s.intervals.empty()||now<s.last||now-s.last>2500)continue;if(s.intervals.size()>r.samples){r={1000*s.intervals.size()/s.total,s.intervals.size(),name,now-s.last,true};}}
        return r;
    }
};
}
