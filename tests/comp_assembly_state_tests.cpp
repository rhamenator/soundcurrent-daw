// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid assembly state accepted");}
}
int main(int argc,char **argv){try{
    check(argc==2,"Expected evidence directory");
    const auto root=std::filesystem::absolute(utf8Path(argv[1]))/utf8Path("assembly-é-日本-"+Id::generate().str());
    std::filesystem::create_directories(root);auto s=makeOneTrackSession("Assembly recovery","Audio");
    const auto ordinary=s;CompPairSelectionReference pair{Id::generate(),std::string(64,'a')};
    applySessionEdits(s,{AddCompPairSelection{pair}});
    // Real mono PCM16 source: project persistence verifies every referenced file.
    std::filesystem::create_directory(root/"media");
    {std::ofstream wave(root/"media/source.wav",std::ios::binary);
        const auto u16=[&](unsigned v){wave.put(char(v&255));wave.put(char((v>>8)&255));};
        const auto u32=[&](unsigned v){u16(v&65535);u16(v>>16);};
        wave.write("RIFF",4);u32(236);wave.write("WAVEfmt ",8);u32(16);u16(1);u16(1);
        u32(48000);u32(96000);u16(2);u16(16);wave.write("data",4);u32(200);
        for(unsigned i=0;i<100;++i) { u16(0); }
        wave.close();check(bool(wave),"Source fixture write failed");}
    Asset a;a.frames=100;a.relativePath="media/source.wav";a.sha256=hashMediaFile(root/"media/source.wav");s.assets.push_back(a);
    Clip clip;clip.assetId=a.id;clip.lengthFrames=100;s.tracks[0].clips.push_back(clip);
    TakeLane lane;lane.name="Original";lane.captureEndFrame=100;clip.id=Id::generate();lane.clips.push_back(clip);s.tracks[0].takeLanes.push_back(lane);
    validate(s);const auto initial=s;
    CompAssemblyReference r{Id::generate(),pair.operation,std::string(64,'c'),{Id::generate(),Id::generate()}};
    EditHistory history(s);check(history.structural({AddCompAssembly{r}}),"Assembly registration missing");const auto selected=s;
    const auto encoded=encodeProject(s);const auto document=Json::parse(encoded);
    check(document.at("schemaMinor")==22 && decodeProject(encoded)==s,"Assembly schema roundtrip failed");
    check(!history.structural({AddCompAssembly{r}}),"Idempotent registration created Undo");
    check(history.undo() && s==initial,"Registration Undo changed source state");
    check(history.redo() && s==selected,"Registration Redo lost trusted binding");
    check(history.structural({RemoveCompAssembly{r.operation}}) && s==initial,"Removal failed");
    check(history.undo() && s==selected,"Removal Undo failed");
    refuses([&]{applySessionEdits(s,{RemoveCompPairSelection{pair.operation}});});check(s==selected,"Dangling parent refusal changed state");
    check(history.structural({RemoveCompAssembly{r.operation},RemoveCompPairSelection{pair.operation}}),"Paired reference removal refused");
    check(history.undo() && s==selected,"Paired reference Undo was not atomic");
    auto conflicting=r;conflicting.requestSha256=std::string(64,'d');
    refuses([&]{applySessionEdits(s,{AddCompAssembly{conflicting}});});check(s==selected,"Conflict changed project");
    const std::vector<Id> occupied{s.id,s.tracks[0].id,s.tracks[0].eq.id,s.tracks[0].eq.bands[0].id,
        a.id,s.tracks[0].clips[0].id,lane.id,lane.groupId,lane.clips[0].id,pair.operation,r.operation,*r.outputs[0],*r.outputs[1]};
    for(const auto &id:occupied)for(unsigned slot=0;slot<3;++slot){
        auto bad=CompAssemblyReference{Id::generate(),pair.operation,std::string(64,'e'),{Id::generate(),Id::generate()}};
        if(slot==0)bad.operation=id;else bad.outputs[slot-1]=id;
        refuses([&]{applySessionEdits(s,{AddCompAssembly{bad}});});check(s==selected,"UUID refusal partially changed project");
    }
    for(unsigned mode=0;mode<11;++mode){auto bad=document;auto &row=bad["compAssemblies"][0];
        if(mode==0)row["requestSha256"]="short";
        if(mode==1)row["requestSha256"]=std::string(64,'A');
        if(mode==2)row["selectionOperation"]=Id::generate().str();
        if(mode==3)row["outputs"][1]=row["outputs"][0];
        if(mode==4)row["outputs"]=Json::array({nullptr,nullptr});
        if(mode==5)row["unknown"]=true;
        if(mode==6)row["requestSha256"]=true;
        if(mode==7)row["outputs"].push_back(nullptr);
        if(mode==8)bad.erase("compAssemblies");
        if(mode==9)bad["schemaMinor"]=21;
        if(mode==10)bad["compAssemblies"].push_back(row);
        refuses([&]{decodeProject(bad.dump());});
    }
    const auto duplicate=encoded.substr(0,1)+std::string("\"compAssemblies\":[],")+encoded.substr(1);
    refuses([&]{decodeProject(duplicate);});
    check(Json::parse(encodeProject(ordinary)).at("schemaMinor")==18 && decodeProject(encodeProject(ordinary))==ordinary,"Ordinary schema changed");
    check(Json::parse(encodeProject(initial)).at("schemaMinor")==21 && decodeProject(encodeProject(initial))==initial,"Previous schema changed");
    ProjectStore store(root);store.save(initial);store.save(selected);
    check(store.load()==selected && store.loadPrevious()==initial,"Durable generations changed");
    SaveOptions cancel;cancel.beforePublish=[]{throw ProjectError(ErrorCode::Canceled,"Injected publication cancellation");};
    refuses([&]{store.save(initial,cancel);});check(store.load()==selected,"Canceled save lost references");
    check(!std::filesystem::exists(root/"media/comp-assembly-requests") && store.load()==selected,"Missing sidecars prevented project load");
    auto relocated=root;relocated+="-relocated";std::filesystem::copy(root,relocated,std::filesystem::copy_options::recursive);
    check(ProjectStore(relocated).load()==selected,"Relocation changed canonical references");
    auto changed=selected;changed.compAssemblies[0].requestSha256=std::string(64,'d');history.adopt(changed);
    s.compAssemblies[0].requestSha256=std::string(64,'e');const auto before=s;refuses([&]{history.undo();});check(s==before,"Conflicting Undo changed model");
    auto many=initial;for(unsigned i=0;i<128;++i)many.compAssemblies.push_back({Id::generate(),pair.operation,std::string(64,'f'),{Id::generate(),std::nullopt}});
    validate(many);check(decodeProject(encodeProject(many))==many,"Reference collection truncated");
    refuses([&]{validate(many,StateBudget{sessionPayloadBytes(initial)});});
    ProjectBudget small;small.encodedBytes=encoded.size()-1;refuses([&]{decodeProject(encoded,small);});
    auto limited=initial;EditHistory constrained(limited,{},HistoryBudget{128,1024*1024,256});
    refuses([&]{constrained.structural({AddCompAssembly{r}});});check(limited==initial,"History quota refusal changed state");
    Json report{{"checks",checks},{"schema","1.22"},{"collisionIdentities",occupied.size()},
        {"saveReopenRelocation",true},{"undoRedo",true},{"missingSidecarProjectReadable",true},{"audioAdoption",false}};
    std::ofstream file(root/"report.json");file<<report.dump(2)<<'\n';check(bool(file),"Report write failed");std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
