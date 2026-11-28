// Retail MDL 44 facial controllers, rules and vertex deltas.
// Layout and flex math follow SE2007 studio.h, studio.cpp and r_studioflex.cpp.
#ifndef ENGINE_MDL44_FLEX_H
#define ENGINE_MDL44_FLEX_H
#include "mdl44_animation.h"
#include "mdl44_sequence.h"
#include <algorithm>
#include <vector>
#include <string>
namespace Mdl44Flex
{
    struct Controller { std::string name, type; float min, max; };
    struct Op { int code, index; float value; };
    struct Rule { int flex; std::vector<Op> ops; };
    struct Delta { unsigned short index; unsigned char speed, side; float position[3], normal[3]; };
    struct Flex { int desc, pair; float target[4]; std::vector<Delta> deltas; };
    struct Mesh { unsigned body, model, mesh, vertexCount; std::vector<Flex> flexes; };
    struct Eye { unsigned body, model; int bone; float origin[3], up[3], forward[3], zoffset, radius, irisScale; int upper[3], lower[3], upperLid, lowerLid; float upperTarget[3], lowerTarget[3]; bool nonFACS; };
    struct Mouth { int bone, flexdesc; float forward[3]; };
    struct EyeMesh { unsigned body, model, mesh, eye; };
    struct Data { std::vector<Mouth> mouths; std::vector<Eye> eyes; std::vector<EyeMesh> eyeMeshes; std::vector<std::string> descriptions; std::vector<Controller> controllers; std::vector<Rule> rules; std::vector<Mesh> meshes; };
    inline int I32(const unsigned char *p,size_t at) { return Mdl44Skeleton::ReadI32(p,at); }
    inline float F32(const unsigned char *p,size_t at) { float v; std::memcpy(&v,p+at,4); return v; }
    inline bool Span(size_t at,size_t count,size_t stride,size_t len) { return at<=len && (!stride || count<=(len-at)/stride); }
    inline bool Relative(size_t base,int offset,size_t len,size_t &out) {
        if(offset<0 || base>len || static_cast<size_t>(offset)>len-base) return false;
        out=base+static_cast<size_t>(offset); return true;
    }
    inline bool Name(const unsigned char *p,size_t len,size_t base,int offset,std::string &out) {
        size_t at; if(!Relative(base,offset,len,at) || at>=len) return false;
        const char *a=reinterpret_cast<const char*>(p+at);
        const char *z=static_cast<const char*>(std::memchr(a,0,len-at)); if(!z) return false;
        out.assign(a,z); return true;
    }
    inline bool Parse(const unsigned char *p,size_t size,Data &out) {
        Studio44::studiohdr_t h; if(!Studio44::ReadHeader(p,size,304,h)) return false;
        const size_t len=static_cast<size_t>(h.length);
        if(h.numflexdesc<0 || h.numflexdesc>1024 || h.numflexcontrollers<0 || h.numflexcontrollers>256 ||
           h.numflexrules<0 || h.numflexrules>1024 || h.numbodyparts<0 || h.numbodyparts>256 ||
           h.flexdescindex<0 || h.flexcontrollerindex<0 || h.flexruleindex<0 || h.bodypartindex<0 ||
           !Span(h.flexdescindex,h.numflexdesc,4,len) || !Span(h.flexcontrollerindex,h.numflexcontrollers,20,len) ||
           !Span(h.flexruleindex,h.numflexrules,12,len) || !Span(h.bodypartindex,h.numbodyparts,16,len)) return false;
        if(h.nummouths<0 || h.nummouths>32 || h.mouthindex<0 || !Span(h.mouthindex,h.nummouths,20,len)) return false;
        Data d;
        for(int i=0;i<h.nummouths;++i) { size_t at=h.mouthindex+size_t(i)*20; Mouth m;
            m.bone=I32(p,at); m.flexdesc=I32(p,at+16);
            if(m.bone<0 || m.bone>=h.numbones || m.flexdesc<0 || m.flexdesc>=h.numflexdesc) return false;
            for(int j=0;j<3;++j) { m.forward[j]=F32(p,at+4+j*4); if(!std::isfinite(m.forward[j])) return false; }
            d.mouths.push_back(m);
        }
        for(int i=0;i<h.numflexdesc;++i) { size_t at=h.flexdescindex+i*4; std::string n; if(!Name(p,len,at,I32(p,at),n)) return false; d.descriptions.push_back(n); }
        for(int i=0;i<h.numflexcontrollers;++i) { size_t at=h.flexcontrollerindex+i*20; Controller c;
            if(!Name(p,len,at,I32(p,at),c.type) || !Name(p,len,at,I32(p,at+4),c.name)) return false;
            c.min=F32(p,at+12); c.max=F32(p,at+16); if(!std::isfinite(c.min)||!std::isfinite(c.max)||c.max<c.min) return false; d.controllers.push_back(c); }
        for(int i=0;i<h.numflexrules;++i) { size_t at=h.flexruleindex+i*12, start; int count=I32(p,at+4); Rule r; r.flex=I32(p,at);
            if(r.flex<0 || r.flex>=h.numflexdesc || count<0 || count>4096 || !Relative(at,I32(p,at+8),len,start) || !Span(start,count,8,len)) return false;
            for(int j=0;j<count;++j) { Op op; op.code=I32(p,start+j*8); op.index=I32(p,start+j*8+4); op.value=F32(p,start+j*8+4); if(op.code==1&&!std::isfinite(op.value)) return false; r.ops.push_back(op); } d.rules.push_back(r); }
        for(int bi=0;bi<h.numbodyparts;++bi) { size_t body=h.bodypartindex+size_t(bi)*16,models; int mc=I32(p,body+4);
            if(mc<0 || !Relative(body,I32(p,body+12),len,models) || !Span(models,mc,148,len)) return false;
            for(int mi=0;mi<mc;++mi) { size_t model=models+size_t(mi)*148,meshes; int meshCount=I32(p,model+72);
                if(meshCount<0 || !Relative(model,I32(p,model+76),len,meshes) || !Span(meshes,meshCount,116,len)) return false;
                const int eyeCount=I32(p,model+100); size_t eyeStart;
                if(eyeCount<0 || eyeCount>16 || !Relative(model,I32(p,model+104),len,eyeStart) || !Span(eyeStart,eyeCount,172,len)) return false;
                for(int ei=0;ei<eyeCount;++ei) {
                    size_t at=eyeStart+size_t(ei)*172; Eye e; e.body=bi; e.model=mi; e.bone=I32(p,at+4);
                    e.zoffset=F32(p,at+20); e.radius=F32(p,at+24); e.irisScale=F32(p,at+60);
                    e.upperLid=I32(p,at+116); e.lowerLid=I32(p,at+120); e.nonFACS=p[at+140]!=0;
                    if(e.bone<0 || e.bone>=h.numbones || !std::isfinite(e.zoffset) || !std::isfinite(e.radius) || e.radius<=0 || !std::isfinite(e.irisScale) || e.irisScale<=0) return false;
                    if(!e.nonFACS && (e.upperLid<0 || e.upperLid>=h.numflexdesc || e.lowerLid<0 || e.lowerLid>=h.numflexdesc)) return false;
                    for(int j=0;j<3;++j) {
                        e.origin[j]=F32(p,at+8+j*4); e.up[j]=F32(p,at+28+j*4); e.forward[j]=F32(p,at+40+j*4);
                        e.upper[j]=I32(p,at+68+j*4); e.lower[j]=I32(p,at+80+j*4);
                        e.upperTarget[j]=F32(p,at+92+j*4); e.lowerTarget[j]=F32(p,at+104+j*4);
                        if(!std::isfinite(e.origin[j]) || !std::isfinite(e.up[j]) || !std::isfinite(e.forward[j]) ||
                           !std::isfinite(e.upperTarget[j]) || !std::isfinite(e.lowerTarget[j])) return false;
                        if(!e.nonFACS && (e.upper[j]<0 || e.upper[j]>=h.numflexdesc || e.lower[j]<0 || e.lower[j]>=h.numflexdesc)) return false;
                    }
                    d.eyes.push_back(e);
                }
                for(int ki=0;ki<meshCount;++ki) { size_t mesh=meshes+size_t(ki)*116,flexStart; int vertices=I32(p,mesh+8),count=I32(p,mesh+16);
                    if(vertices<0 || count<0 || count>4096 || !Relative(mesh,I32(p,mesh+20),len,flexStart) || !Span(flexStart,count,60,len)) return false;
                    if(I32(p,mesh+24)==1) { const int eye=I32(p,mesh+28); if(eye<0 || eye>=eyeCount) return false; EyeMesh em; em.body=bi;em.model=mi;em.mesh=ki;em.eye=static_cast<unsigned>(d.eyes.size()-eyeCount+eye);d.eyeMeshes.push_back(em); }
                    if(!count) continue; Mesh m; m.body=bi;m.model=mi;m.mesh=ki;m.vertexCount=vertices;
                    for(int fi=0;fi<count;++fi) { size_t at=flexStart+size_t(fi)*60,verts; Flex f;f.desc=I32(p,at);f.pair=I32(p,at+28);int nv=I32(p,at+20);const unsigned type=p[at+32];
                        if(f.desc<0 || f.desc>=h.numflexdesc || f.pair<0 || f.pair>=h.numflexdesc || nv<0 || type>1 ||
                           !Relative(at,I32(p,at+24),len,verts) || !Span(verts,nv,type?18:16,len)) return false;
                        for(int t=0;t<4;++t) { f.target[t]=F32(p,at+4+t*4);if(!std::isfinite(f.target[t])||(t&&f.target[t]<f.target[t-1])) return false; }
                        for(int vi=0;vi<nv;++vi) { const size_t v=verts+size_t(vi)*(type?18:16); Delta delta;
                            std::memcpy(&delta.index,p+v,2);delta.speed=p[v+2];delta.side=p[v+3]; if(delta.index>=vertices) return false;
                            for(int axis=0;axis<3;++axis) if(!Mdl44Animation::HalfFloat(Mdl44Animation::ReadU16(p,v+4+axis*2),delta.position[axis]) ||
                                !Mdl44Animation::HalfFloat(Mdl44Animation::ReadU16(p,v+10+axis*2),delta.normal[axis])) return false;
                            f.deltas.push_back(delta);
                        }
                        m.flexes.push_back(f);
                    }
                    d.meshes.push_back(m);
                }
            }
        }
        out=d;return true;
    }
    inline float Clamp(float x,float a,float b) { return x<a?a:(x>b?b:x); }
    inline bool Evaluate(const Data &data,const float *controllers,size_t n,std::vector<float> &out) {
        if(!controllers || n<data.controllers.size()) return false; std::vector<float> result(data.descriptions.size(),0);
        for(size_t i=0;i<data.rules.size();++i) { const Rule &rule=data.rules[i]; float stack[32];int k=0;
            for(size_t j=0;j<rule.ops.size();++j) { const Op &op=rule.ops[j];
                switch(op.code) {
                case 1: if(k>=32)return false;stack[k++]=op.value;break;
                case 2: if(k>=32||op.index<0||size_t(op.index)>=n)return false;stack[k++]=controllers[op.index];break;
                case 3: if(k>=32||op.index<0||size_t(op.index)>=result.size())return false;stack[k++]=result[op.index];break;
                case 4: case 5: case 6: case 7: case 13: case 14:
                    if(k<2)return false; { float a=stack[k-2],b=stack[k-1];
                    stack[k-2]=op.code==4?a+b:op.code==5?a-b:op.code==6?a*b:op.code==7?(b>0.0001f?a/b:0):op.code==13?(std::max)(a,b):(std::min)(a,b);--k; }break;
                case 8:if(k<1)return false;stack[k-1]=-stack[k-1];break;
                case 15: case 16: if(k>=32||op.index<0||size_t(op.index)>=n)return false;stack[k++]=op.code==15?Clamp(-controllers[op.index],0,1):Clamp(controllers[op.index],0,1);break;
                case 18: case 19: { int count=op.index;if(count<=0||count>k || (op.code==19&&count==k))return false;int base=k-count;float v=stack[base];for(int t=base+1;t<k;++t)v*=stack[t];if(op.code==18){stack[base]=v;k=base+1;}else{stack[base-1]*=1-v;k=base;} }break;
                default:return false;
                }
            }
            if(k!=1 || !std::isfinite(stack[0]))return false;result[rule.flex]=stack[0];
        }
        out.swap(result);return true;
    }
    // Matches SE2007 R_StudioEyelidFACS; directions are in the eye bone's local space.
    inline bool Eyelids(const Eye &eye, const float localUp[3], const float localForward[3], std::vector<float> &weights) {
        if(eye.nonFACS) return true;
        if(eye.radius<=0 || eye.upperLid<0 || eye.lowerLid<0 || size_t(eye.upperLid)>=weights.size() || size_t(eye.lowerLid)>=weights.size()) return false;
        float upper=0, lower=0;
        for(int i=0;i<3;++i) {
            if(eye.upper[i]<0 || eye.lower[i]<0 || size_t(eye.upper[i])>=weights.size() || size_t(eye.lower[i])>=weights.size()) return false;
            upper+=weights[eye.upper[i]]*std::asin(Clamp(eye.upperTarget[i]/eye.radius,-1,1));
            lower+=weights[eye.lower[i]]*std::asin(Clamp(eye.lowerTarget[i]/eye.radius,-1,1));
        }
        float lidUpper=0,lidLower=0;
        for(int i=0;i<3;++i) {
            lidUpper+=eye.radius*(std::sin(upper)*localUp[i]+std::cos(upper)*localForward[i])*eye.up[i];
            lidLower+=eye.radius*(std::sin(lower)*localUp[i]+std::cos(lower)*localForward[i])*eye.up[i];
        }
        if(!std::isfinite(lidUpper) || !std::isfinite(lidLower)) return false;
        weights[eye.upperLid]=lidUpper; weights[eye.lowerLid]=lidLower; return true;
    }
    inline float Ramp(const Flex &f,float value) {
        if(value<=f.target[0]||value>=f.target[3])return 0;
        if(value<f.target[1])return (value-f.target[0])/(f.target[1]-f.target[0]);
        if(value>f.target[2])return (f.target[3]-value)/(f.target[3]-f.target[2]);
        return 1;
    }
}
#endif
