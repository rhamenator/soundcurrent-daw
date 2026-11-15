// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *message){++checks;if(!v)throw std::runtime_error(message);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid comp project reference accepted");}
}
int main(int argc,char **argv){try{
    check(argc==2,"Expected evidence directory");const auto root=std::filesystem::absolute(utf8Path(argv[1]))/utf8Path("project-é-日本-"+Id::generate().str());
    std::filesystem::create_directories(root);auto session=makeOneTrackSession("Comp recovery","Audio");const auto initial=session;
    CompPairSelectionReference first{Id::generate(),std::string(64,'a')};EditHistory history(session);
    check(history.structural({AddCompPairSelection{first}}),"Reference registration missing");const auto selected=session;
    const auto encoded=encodeProject(session);const auto document=Json::parse(encoded);
    check(document.at("schemaMinor")==21 && decodeProject(encoded)==selected,"References did not roundtrip schema1.21");
    check(!history.structural({AddCompPairSelection{first}}),"Identical reference registration created Undo");
    check(history.undo() && session==initial,"Reference Undo altered audio");check(history.redo() && session==selected,"Reference Redo changed binding");
    check(history.structural({RemoveCompPairSelection{first.operation}}) && session==initial,"Reference removal failed");
    check(history.undo() && session==selected,"Removal Undo lost original binding");
    const auto unchanged=session;auto conflict=first;conflict.selectionSha256=std::string(64,'b');
    refuses([&]{applySessionEdits(session,{AddCompPairSelection{conflict}});});check(session==unchanged,"Conflicting register mutated state");
    refuses([&]{applySessionEdits(session,{RemoveCompPairSelection{Id::generate()}});});
    for(unsigned mode=0;mode<8;++mode){auto bad=document;
        if(mode==0)bad["compPairSelections"][0]["selectionSha256"]="short";
        if(mode==1)bad["compPairSelections"][0]["selectionSha256"]=std::string(64,'A');
        if(mode==2)bad["compPairSelections"][0]["operation"]=session.id.str();
        if(mode==3)bad["compPairSelections"].push_back(bad["compPairSelections"][0]);
        if(mode==4)bad["compPairSelections"][0]["unknown"]=true;
        if(mode==5)bad["compPairSelections"][0]["selectionSha256"]=true;
        if(mode==6)bad.erase("compPairSelections");
        if(mode==7)bad["schemaMinor"]=20;
        refuses([&]{decodeProject(bad.dump());});
    }
    check(Json::parse(encodeProject(initial)).at("schemaMinor")==18,"Unrelated project schema changed");
    ProjectStore store(root);store.save(initial);store.save(selected);check(store.load()==selected && store.loadPrevious()==initial,"Durable reference/previous generation changed");
    SaveOptions canceled;canceled.beforePublish=[] {throw ProjectError(ErrorCode::Canceled,"Test interrupted publication");};
    refuses([&]{store.save(initial,canceled);});check(store.load()==selected,"Canceled project publication lost reference");
    // Missing comp job/manifest is a recovery condition, not loss of the project.
    check(!std::filesystem::exists(root/"media/comp-pairs") && store.load()==selected,"Missing sidecar prevented project load");
    auto altered=selected;altered.compPairSelections[0].selectionSha256=std::string(64,'c');history.adopt(altered);
    session.compPairSelections[0].selectionSha256=std::string(64,'d');const auto beforeConflict=session;
    refuses([&]{history.undo();});check(session==beforeConflict,"Conflicting Undo partially changed model");
    auto many=initial;for(unsigned i=0;i<128;++i)many.compPairSelections.push_back({Id::generate(),std::string(64,'e')});
    validate(many);check(decodeProject(encodeProject(many))==many,"Reference collection truncated");
    StateBudget limited{sessionPayloadBytes(initial)};refuses([&]{validate(many,limited);});
    ProjectBudget small;small.encodedBytes=encoded.size()-1;refuses([&]{decodeProject(encoded,small);});
    auto empty=initial;EditHistory constrained(empty,{},HistoryBudget{128,1024*1024,256});
    refuses([&]{constrained.structural({AddCompPairSelection{first}});});check(empty==initial,"Rejected Undo budget mutated project");
    const auto rootUtf8=root.generic_u8string();const std::string rootText(reinterpret_cast<const char *>(rootUtf8.data()),rootUtf8.size());
    Json result{{"checks",checks},{"schema","1.21"},{"projectRoot",rootText},{"saveReopen",true},{"previousGeneration",true},{"missingSidecarProjectReadable",true},{"undoRedo",true},{"workerLaunched",false},{"audioChanged",false}};
    std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
