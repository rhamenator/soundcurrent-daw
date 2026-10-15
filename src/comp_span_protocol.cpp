// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_span_protocol.hpp>
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <soundcurrent/comp_regeneration_source.hpp>
#include "media_io.hpp"
#include "warp_codec.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <set>
#include <vector>
namespace soundcurrent::daw {
namespace {
using Json=nlohmann::json;
void check(bool v,const char *m,ErrorCode code=ErrorCode::InvalidState){if(!v)throw ProjectError(code,m);}
bool sha(std::string_view s){return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string hash(std::string_view s){media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(s.data()),s.size()});return h.digest();}
void policy(const CompSpanWorkerPolicy &p){check(p.memoryBytes>=128ULL*1024*1024 && p.memoryBytes<=4096ULL*1024*1024 && p.memoryBytes%(1024*1024)==0 &&
    p.maximumSourceBytes>0 && p.maximumSourceBytes<=8ULL*1024*1024*1024 && p.maximumOutputBytes>=4096 && p.maximumOutputBytes<=8ULL*1024*1024*1024 &&
    p.deadlineMilliseconds>=100 && p.deadlineMilliseconds<=3600000,"Invalid comp span worker policy");}
// Admission precedes DOM allocation. Protocol objects are deliberately small;
// the large recipe is a bounded string parsed by its separate admitted codec.
struct Preflight : nlohmann::json_sax<Json> {
    std::vector<std::size_t> counts;std::size_t scalars=0;PayloadCharge charge;
    explicit Preflight(std::size_t maximum):charge("Comp span parser staging",maximum){}
    bool scalar(){check(++scalars<=256,"Comp span scalar count exceeds bound");charge.add(128);if(!counts.empty())check(++counts.back()<=64,"Comp span container exceeds bound");return true;}
    bool null() override {return scalar();}bool boolean(bool) override {return scalar();}
    bool number_integer(number_integer_t) override {return scalar();}bool number_unsigned(number_unsigned_t) override {return scalar();}
    bool number_float(number_float_t,const string_t &) override {return scalar();}
    bool string(string_t &v) override {charge.add(v.size());return scalar();}
    bool binary(binary_t &) override {check(false,"Binary span protocol refused");return false;}
    bool start(){check(counts.size()<4,"Comp span nesting exceeds bound");charge.add(512);counts.push_back(0);return true;}
    bool start_object(std::size_t) override {return start();}bool start_array(std::size_t) override {return start();}
    bool key(string_t &v) override {charge.add(v.size()+128);return true;}
    bool end_object() override {counts.pop_back();return true;}bool end_array() override {counts.pop_back();return true;}
    bool parse_error(std::size_t,const std::string &,const nlohmann::detail::exception &) override {check(false,"Invalid comp span JSON");return false;}
};
Json parse(std::string_view bytes,std::size_t maximum,unsigned depthLimit=4){
    check(!bytes.empty() && bytes.size()<=maximum,"Comp span protocol exceeds bank",ErrorCode::ResourceLimit);std::array<std::set<std::string>,6> keys;
    auto callback=[&](int depth,Json::parse_event_t event,Json &value){check(depth>=0 && unsigned(depth)<depthLimit,"Comp span protocol nesting exceeds bound");
        if(event==Json::parse_event_t::object_start)keys[std::size_t(depth+1)].clear();
        if(event==Json::parse_event_t::key)check(keys[std::size_t(depth)].insert(value.get<std::string>()).second,"Duplicate comp span field");
        return true;};
    Preflight preflight(maximum*3+65536);preflight.charge.add(bytes.size());check(Json::sax_parse(bytes.begin(),bytes.end(),&preflight),"Malformed span protocol");
    try{return Json::parse(bytes.begin(),bytes.end(),callback);}catch(const Json::exception &){throw ProjectError(ErrorCode::InvalidState,"Invalid comp span JSON");}
}
bool historyRequired(const CompProcessingRecipe &r){return bool(r.plan().handles().outgoing().data().regeneration) || bool(r.plan().handles().incoming().data().regeneration);}
Json clockBinding(const CompProcessingRecipe &r,const CompSpanPlan &s,unsigned fragment){
    const auto &e=(fragment?r.plan().handles().incoming():r.plan().handles().outgoing()).data();
    return {{"inputRate",s.source().sampleRate},{"outputRate",s.outputSampleRate()},
        {"backendClockScale",warp_codec::position(s.backendClockScale())},
        {"bakedPitchScale",warp_codec::position(e.regeneration?e.regeneration->data().bakedPitchScale:SourcePosition{1,0,1})},
        {"processingStep",Json::array({s.grid().numerator(),s.grid().denominator()})}};
}
Json protectedHistoryBinding(const CompProcessingRecipe &r,unsigned fragment){
    const auto &e=(fragment?r.plan().handles().incoming():r.plan().handles().outgoing()).data();
    const auto &original=e.regeneration?e.regeneration->data().originalNominalClip:e.expected;
    return {{"inputRate",e.raw.sampleRate},{"outputRate",e.base.sampleRate},{"regenerated",bool(e.regeneration)},
        {"backendClockScale",warp_codec::position(e.regeneration?e.regeneration->data().backendClockScale:SourcePosition{1,0,1})},
        {"bakedPitchScale",warp_codec::position(e.regeneration?e.regeneration->data().bakedPitchScale:SourcePosition{1,0,1})},
        {"originalClip",original.id.str()}};
}
Json protectedBinding(const CompProtectedSpanPlan &p,ResourceLedger ledger){
    const auto &g=p.geometry();const auto &region=g.map().region();const auto policy=g.policy();
    PayloadCharge staging("Protected span binding",SIZE_MAX);staging.add(65536);
    staging.add(p.rawMap().size(),2048);staging.add(g.spans().size(),4096);auto credit=ledger.reserve(staging.bytes());
    Json points=Json::array(),spans=Json::array();
    for(const auto &point:p.rawMap())points.push_back(Json::array({warp_codec::position(point.raw),warp_codec::position(point.output)}));
    for(const auto &span:g.spans())spans.push_back({{"owner",span.owner.str()},
        {"sourceBegin",warp_codec::position(span.sourceBegin)},{"sourceAnchor",warp_codec::position(span.sourceAnchor)},{"sourceEnd",warp_codec::position(span.sourceEnd)},
        {"outputBegin",warp_codec::position(span.outputBegin)},{"outputAnchor",warp_codec::position(span.outputAnchor)},{"outputEnd",warp_codec::position(span.outputEnd)}});
    Json geometry={{"processor",compProtectedSpanProcessorId},{"sourceId",p.source().id.str()},{"sourceSha256",p.source().sha256},
        {"rate",p.source().sampleRate},{"layoutKind",unsigned(p.source().layout.kind)},{"channels",p.source().layout.channels},
        {"rawOrigin",warp_codec::position(region.rawOrigin)},{"rawFrames",region.inputFrames},{"outputFrames",region.outputFrames},
        {"before",policy.before},{"after",policy.after},{"halo",policy.halo},{"minimumNonunityGap",policy.minimumNonunityGap},
        {"chunkFrames",512},{"map",clipWarpGeometryId},{"rawMap",std::move(points)},{"semanticSpans",std::move(spans)}};
    const auto bytes=geometry.dump();check(bytes.size()<=2*1024*1024,"Protected span binding exceeds bound",ErrorCode::ResourceLimit);
    return {{"geometrySha256",hash(bytes)},{"rawOrigin",warp_codec::position(region.rawOrigin)},
        {"rawFrames",region.inputFrames},{"outputFrames",region.outputFrames},{"inputRate",p.source().sampleRate},
        {"outputRate",p.source().sampleRate},{"semanticAnchors",g.spans().size()},{"chunkFrames",512}};
}
Json clockedBinding(const CompClockedProtectedSpanPlan &p,bool history){
    const auto &clock=p.sampling().clock();
    return {{"inputRate",p.source().sampleRate},{"outputRate",p.outputSampleRate()},
        {"origin",warp_codec::position(scaleSourcePosition(clock.at(0),1,1))},
        {"step",warp_codec::position(scaleSourcePosition({1,0,1},clock.numerator(),clock.denominator()))},
        {"bakedPitchScale",warp_codec::position(p.bakedPitchScale())},{"historyRequired",history}};
}
Json clockedProtectedBinding(const CompClockedProtectedSpanPlan &p,ResourceLedger ledger){
    const auto &g=p.geometry();const auto &region=g.map().exactRegion();const auto policy=g.policy();
    PayloadCharge staging("Clocked protected binding",SIZE_MAX);staging.add(65536);
    staging.add(g.map().points().size(),2048);staging.add(g.spans().size(),4096);auto credit=ledger.reserve(staging.bytes());
    Json points=Json::array(),spans=Json::array();
    for(const auto &point:g.map().points())points.push_back(Json::array({warp_codec::position(point.source),warp_codec::position(point.output)}));
    for(const auto &span:g.spans())spans.push_back({{"owner",span.owner.str()},
        {"sourceBegin",warp_codec::position(span.sourceBegin)},{"sourceAnchor",warp_codec::position(span.sourceAnchor)},{"sourceEnd",warp_codec::position(span.sourceEnd)},
        {"outputBegin",warp_codec::position(span.outputBegin)},{"outputAnchor",warp_codec::position(span.outputAnchor)},{"outputEnd",warp_codec::position(span.outputEnd)}});
    Json geometry={{"processor",compClockedProtectedSpanProcessorId},{"algorithm",clockedProtectedWarpAlgorithmId},
        {"resampling",positionedResamplingAlgorithmId},{"map",clipWarpGeometryId},
        {"sourceId",p.source().id.str()},{"sourceSha256",p.source().sha256},{"sourceFrames",p.source().frames},
        {"rate",p.source().sampleRate},{"layoutKind",unsigned(p.source().layout.kind)},{"channels",p.source().layout.channels},
        {"rawOrigin",warp_codec::position(region.rawOrigin)},{"inputExtent",warp_codec::position(region.inputExtent)},
        {"outputExtent",warp_codec::position(region.outputExtent)},
        {"before",policy.before},{"after",policy.after},{"halo",policy.halo},{"minimumNonunityGap",policy.minimumNonunityGap},
        {"chunkFrames",512},{"points",std::move(points)},{"semanticSpans",std::move(spans)}};
    const auto bytes=geometry.dump();check(bytes.size()<=2*1024*1024,"Clocked protected binding exceeds bound",ErrorCode::ResourceLimit);
    return {{"geometrySha256",hash(bytes)},{"rawOrigin",warp_codec::position(region.rawOrigin)},
        {"inputExtent",warp_codec::position(region.inputExtent)},{"outputExtent",warp_codec::position(region.outputExtent)},
        {"outputFrames",p.sampling().outputFrames()},{"semanticAnchors",g.spans().size()},{"chunkFrames",512}};
}
Json uniformContextBinding(const CompUniformContextPlan &p,bool history){
    return {{"sourceId",p.source().id.str()},{"sourceSha256",p.source().sha256},{"sourceFrames",p.source().frames},
        {"layoutKind",unsigned(p.source().layout.kind)},{"channels",p.source().layout.channels},
        {"inputRate",p.source().sampleRate},{"outputRate",p.outputSampleRate()},
        {"rawOrigin",warp_codec::position(p.input().at(0))},{"rawExtent",warp_codec::position(p.rawExtent())},
        {"inputFrames",p.inputFrames()},{"inputStep",Json::array({p.input().numerator(),p.input().denominator()})},
        {"intrinsicExtent",warp_codec::position(p.intrinsicExtent())},{"renderFrames",p.renderFrames()},
        {"candidateOrigin",warp_codec::position(p.samples().at(0))},{"candidateStep",Json::array({p.samples().numerator(),p.samples().denominator()})},
        {"outputFrames",p.outputFrames()},{"bakedPitchScale",warp_codec::position(p.bakedPitchScale())},
        {"pitchMilliCents",p.settings().pitchMilliCents},{"formantPreserved",p.settings().formantPreserved},
        {"chunkFrames",512},{"resampling",positionedResamplingAlgorithmId},{"historyRequired",history}};
}
Json intent(const CompSpanRequest &r,const CompSpanWorkerPolicy &p,ResourceLedger ledger){policy(p);
    if(r.isUniformContext()){
        const auto &a=r.source();check(std::uint64_t(r.outputFrames())<=(p.maximumOutputBytes-std::min<std::uint64_t>(1048576,p.maximumOutputBytes))/(a.layout.channels*4ULL),"Uniform context span exceeds output grant",ErrorCode::ResourceLimit);
        return {{"protocol",r.protocol()},{"processor",r.processor()},{"sourceAlgorithm",positionedResamplingAlgorithmId},
            {"operation",r.operation().str()},{"recipeDigest",r.recipe().digest()},{"requestDigest",r.digest()},{"fragment",r.fragment()},
            {"executorSha256",r.executorSha256()},{"sourceSha256",a.sha256},{"outputFrames",r.outputFrames()},
            {"rate",r.outputSampleRate()},{"channels",a.layout.channels},{"uniformContext",uniformContextBinding(r.uniformContextSpan(),r.requiresHistory())},
            {"memoryCeilingBytes",p.memoryBytes},{"deadlineMilliseconds",p.deadlineMilliseconds},
            {"maximumSourceBytes",p.maximumSourceBytes},{"maximumOutputBytes",p.maximumOutputBytes}};
    }
    if(r.isProtected()){
        const auto &a=r.source();check(std::uint64_t(r.outputFrames())<=(p.maximumOutputBytes-std::min<std::uint64_t>(1048576,p.maximumOutputBytes))/(a.layout.channels*4ULL),"Protected span exceeds output grant",ErrorCode::ResourceLimit);
        Json result={{"protocol",r.protocol()},{"processor",r.processor()},{"sourceAlgorithm","soundcurrent.raw-absolute-frames-v1"},
            {"operation",r.operation().str()},{"recipeDigest",r.recipe().digest()},{"requestDigest",r.digest()},{"fragment",r.fragment()},
            {"executorSha256",r.executorSha256()},{"sourceSha256",a.sha256},{"outputFrames",r.outputFrames()},
            {"rate",r.outputSampleRate()},{"channels",a.layout.channels},{"protected",r.isClockedProtected()?clockedProtectedBinding(r.clockedProtectedSpan(),ledger):protectedBinding(r.protectedSpan(),ledger)},
            {"memoryCeilingBytes",p.memoryBytes},{"deadlineMilliseconds",p.deadlineMilliseconds},
            {"maximumSourceBytes",p.maximumSourceBytes},{"maximumOutputBytes",p.maximumOutputBytes}};
        if(r.isClockedProtected())result["clock"]=clockedBinding(r.clockedProtectedSpan(),r.requiresHistory());
        else if(r.requiresHistory())result["clock"]=protectedHistoryBinding(r.recipe(),r.fragment());
        return result;
    }
    const auto &s=r.span();const auto &a=s.source();
    check(std::uint64_t(s.outputFrames())<=(p.maximumOutputBytes-std::min<std::uint64_t>(1048576,p.maximumOutputBytes))/(a.layout.channels*4ULL),"Comp span exceeds output grant",ErrorCode::ResourceLimit);
    Json result={{"protocol",r.protocol()},{"processor",r.processor()},{"sourceAlgorithm",positionedResamplingAlgorithmId},{"operation",r.operation().str()},
        {"recipeDigest",r.recipe().digest()},{"requestDigest",r.digest()},{"fragment",r.fragment()},{"executorSha256",r.executorSha256()},
        {"sourceSha256",a.sha256},{"rawOrigin",warp_codec::position(scaleSourcePosition(s.grid().at(0),1,1))},{"rawSpan",warp_codec::position(s.rawSpan())},
        {"processingFrames",s.processingFrames()},{"outputFrames",s.outputFrames()},{"processingStep",Json::array({s.grid().numerator(),s.grid().denominator()})},
        {"rate",s.outputSampleRate()},{"channels",a.layout.channels},{"pitchMilliCents",s.settings().pitchMilliCents},{"formantPreserved",s.settings().formantPreserved},
        {"memoryCeilingBytes",p.memoryBytes},{"deadlineMilliseconds",p.deadlineMilliseconds},{"maximumSourceBytes",p.maximumSourceBytes},{"maximumOutputBytes",p.maximumOutputBytes}};
    if(r.requiresHistory())result["clock"]=clockBinding(r.recipe(),s,r.fragment());
    return result;
}
WaveValidationLimits waveLimits(const Asset &a,std::uint64_t cap){WaveValidationLimits l;l.maximumFrames=std::uint64_t(a.frames);l.maximumChannels=a.layout.channels;l.blockFrames=512;l.maximumBytesRead=cap*4+65536;l.maximumIoOperations=16000000;return l;}
}
const CompSpanPlan &CompSpanRequest::span() const {
    const auto *p=std::get_if<CompSpanPlan>(&span_);check(p!=nullptr,"Protected span is not a uniform plan");return *p;
}
const CompProtectedSpanPlan &CompSpanRequest::protectedSpan() const {
    const auto *p=std::get_if<CompProtectedSpanPlan>(&span_);check(p!=nullptr,"Uniform span is not a protected plan");return *p;
}
const CompClockedProtectedSpanPlan &CompSpanRequest::clockedProtectedSpan() const {
    const auto *p=std::get_if<CompClockedProtectedSpanPlan>(&span_);check(p!=nullptr,"Span is not a clocked protected plan");return *p;
}
const CompUniformContextPlan &CompSpanRequest::uniformContextSpan() const {
    const auto *p=std::get_if<CompUniformContextPlan>(&span_);check(p!=nullptr,"Span is not an original uniform context plan");return *p;
}
const Asset &CompSpanRequest::source() const {return std::visit([](const auto &p)->const Asset &{return p.source();},span_);}
Frame CompSpanRequest::outputFrames() const {return isUniformContext()?uniformContextSpan().outputFrames():isClockedProtected()?clockedProtectedSpan().sampling().outputFrames():isProtected()?protectedSpan().geometry().map().region().outputFrames:span().outputFrames();}
std::uint32_t CompSpanRequest::outputSampleRate() const {return isUniformContext()?uniformContextSpan().outputSampleRate():isClockedProtected()?clockedProtectedSpan().outputSampleRate():isProtected()?source().sampleRate:span().outputSampleRate();}
CompRequestedSpanReport renderRequestedCompSpan(const CompSpanRequest &r,const CompSpanRead &read,const CompSpanWrite &write,ResourceLedger ledger,const std::function<void()> &poll){
    if(r.isUniformContext()){const auto result=renderUniformContextCompSpan(r.uniformContextSpan(),read,write,ledger,poll);return {result.writtenFrames,result.floatPeak};}
    if(r.isClockedProtected()){const auto result=renderClockedProtectedCompSpan(r.clockedProtectedSpan(),read,write,ledger,poll);return {result.writtenFrames,result.floatPeak};}
    if(r.isProtected()){const auto result=renderProtectedCompSpan(r.protectedSpan(),read,write,ledger,poll);return {result.writtenFrames,result.floatPeak};}
    const auto result=renderCompSpan(r.span(),read,write,ledger,poll);return {result.writtenFrames,result.floatPeak};
}
CompSpanRequest decodeCompSpanRequest(std::string_view bytes,std::string_view expectedDigest,const CompHandlePlanContext &context,std::string_view executable,ResourceLedger ledger){
    check(sha(expectedDigest) && sha(executable),"Invalid trusted span identities");check(bytes.size()<=compSpanRequestMaximum,"Span request too large",ErrorCode::ResourceLimit);
    auto work=ledger.reserve(16*1024*1024);auto retained=ledger.reserve(bytes.size()*2+65536);check(hash(bytes)==expectedDigest,"Span request changed from trusted parent",ErrorCode::MediaMismatch);
    try{const auto j=parse(bytes,compSpanRequestMaximum);check(j.is_object() && j.at("protocol").is_string(),"Unknown comp span schema");
        const auto &protocol=j.at("protocol").get_ref<const std::string &>();const bool uniformContext=protocol==compUniformContextSpanProtocol,clocked=protocol==compClockedProtectedSpanProtocol,protectedHistory=protocol==compProtectedHistorySpanProtocol,protectedMode=clocked || protocol==compProtectedSpanProtocol || protectedHistory;
        bool history=protocol==compRegenerationSpanProtocol || protectedHistory;
        check((uniformContext || protocol==compSpanProtocol || history || protectedMode) && j.size()==8+std::size_t(history || clocked || uniformContext)+std::size_t(protectedMode),"Unknown comp span schema");
        check(warp_codec::exact(j.at("executor"),Json{{"id",uniformContext?compUniformContextProcessorId:clocked?compClockedProtectedSpanProcessorId:protectedMode?(history?compProtectedHistorySpanProcessorId:compProtectedSpanProcessorId):(history?compRegenerationSpanProcessorId:compSpanProcessorId)},{"buildSha256",executable}}),"Span executable differs from trusted request");
        check(j.at("recipeExecutor").is_object() && j.at("recipeExecutor").size()==2,"Invalid recipe executor");
        const CompRecipeExecutor parent{j.at("recipeExecutor").at("id").get<std::string>(),j.at("recipeExecutor").at("buildSha256").get<std::string>()};
        const auto operation=Id(j.at("operation").get<std::string>()),recipeOperation=Id(j.at("recipeOperation").get<std::string>());check(operation!=recipeOperation,"Span reused parent operation");
        const auto fragment=unsigned(warp_codec::unsignedInteger(j.at("fragment"),1));
        auto recipe=decodeCompProcessingRecipe(j.at("recipe").get_ref<const std::string &>(),recipeOperation,context,parent,j.at("recipeDigest").get<std::string>(),ledger);
        if(clocked || uniformContext)history=historyRequired(recipe);
        check(history==historyRequired(recipe),"Span protocol does not match original history requirements");
        const auto &handles=recipe.plan().handles();const auto &envelope=fragment?handles.incoming():handles.outgoing();
        CompSpanPayload span=[&]() -> CompSpanPayload {if(uniformContext)return prepareUniformContextCompSpan(envelope,ledger,[]{});if(clocked)return prepareClockedProtectedCompSpan(envelope,ledger,[]{});if(protectedMode)return prepareProtectedCompSpan(envelope,ledger,[]{});return prepareCompSpan(envelope,ledger);}();
        if(uniformContext)check(warp_codec::exact(j.at("uniformContext"),uniformContextBinding(std::get<CompUniformContextPlan>(span),history)),"Uniform context binding differs from original source/history");
        else if(clocked)check(warp_codec::exact(j.at("clock"),clockedBinding(std::get<CompClockedProtectedSpanPlan>(span),history)),"Clocked protected timing differs from original history");
        else if(history)check(warp_codec::exact(j.at("clock"),protectedMode?protectedHistoryBinding(recipe,fragment):clockBinding(recipe,std::get<CompSpanPlan>(span),fragment)),"Span clock binding differs from original source history");
        if(clocked)check(warp_codec::exact(j.at("protected"),clockedProtectedBinding(std::get<CompClockedProtectedSpanPlan>(span),ledger)),"Clocked protected geometry differs from original history");
        else if(protectedMode)check(warp_codec::exact(j.at("protected"),protectedBinding(std::get<CompProtectedSpanPlan>(span),ledger)),"Protected binding differs from original semantic geometry");
        else if(!history && !uniformContext){const auto &uniform=std::get<CompSpanPlan>(span);check(uniform.outputSampleRate()==uniform.source().sampleRate && uniform.backendClockScale()==SourcePosition{1,0,1},"Legacy span has a nonlegacy sample clock");}
        for(const auto *envelope:{&handles.outgoing(),&handles.incoming()}){const auto &e=envelope->data();check(operation!=e.raw.id && operation!=e.base.id && operation!=e.retainedAsset.id && (!e.nominalAsset || operation!=e.nominalAsset->id),"Span reused an input identity");}
        return {std::move(retained),std::move(recipe),std::move(span),operation,fragment,std::string(expectedDigest),std::string(executable),std::string(bytes),history};
    }catch(const Json::exception &){throw ProjectError(ErrorCode::InvalidState,"Invalid comp span request fields");}
}
CompSpanRequest encodeCompSpanRequest(const CompProcessingRecipe &r,const CompRecipeExecutor &parent,unsigned fragment,const Id &op,std::string_view executable,ResourceLedger ledger){
    auto work=ledger.reserve(16*1024*1024);auto encoded=ledger.reserve(compSpanRequestMaximum*2);check(fragment<=1 && sha(executable),"Invalid span selection/executor");
    const bool history=historyRequired(r);
    Json document={{"protocol",history?compRegenerationSpanProtocol:compSpanProtocol},{"operation",op.str()},{"recipeOperation",r.operation().str()},{"recipeDigest",r.digest()},{"recipe",r.bytes()},
        {"recipeExecutor",{{"id",parent.id},{"buildSha256",parent.buildSha256}}},{"executor",{{"id",history?compRegenerationSpanProcessorId:compSpanProcessorId},{"buildSha256",executable}}},{"fragment",fragment}};
    if(history){auto span=prepareCompSpan(fragment?r.plan().handles().incoming():r.plan().handles().outgoing(),ledger);document["clock"]=clockBinding(r,span,fragment);}
    const auto bytes=document.dump();
    const auto context=r.plan().handles().outgoing().data().context;work.resize(0);return decodeCompSpanRequest(bytes,hash(bytes),context,executable,ledger);
}
CompSpanRequest encodeProtectedCompSpanRequest(const CompProcessingRecipe &r,const CompRecipeExecutor &parent,unsigned fragment,const Id &op,std::string_view executable,ResourceLedger ledger){
    auto work=ledger.reserve(16*1024*1024);auto encoded=ledger.reserve(compSpanRequestMaximum*2);
    check(fragment<=1 && sha(executable),"Invalid protected span selection/executor");
    const bool history=historyRequired(r);
    auto span=prepareProtectedCompSpan(fragment?r.plan().handles().incoming():r.plan().handles().outgoing(),ledger,[]{});
    Json document={{"protocol",history?compProtectedHistorySpanProtocol:compProtectedSpanProtocol},{"operation",op.str()},{"recipeOperation",r.operation().str()},
        {"recipeDigest",r.digest()},{"recipe",r.bytes()},{"recipeExecutor",{{"id",parent.id},{"buildSha256",parent.buildSha256}}},
        {"executor",{{"id",history?compProtectedHistorySpanProcessorId:compProtectedSpanProcessorId},{"buildSha256",executable}}},{"fragment",fragment},{"protected",protectedBinding(span,ledger)}};
    if(history)document["clock"]=protectedHistoryBinding(r,fragment);
    const auto bytes=document.dump();const auto context=r.plan().handles().outgoing().data().context;work.resize(0);
    return decodeCompSpanRequest(bytes,hash(bytes),context,executable,ledger);
}
CompSpanRequest encodeClockedProtectedCompSpanRequest(const CompProcessingRecipe &r,const CompRecipeExecutor &parent,unsigned fragment,const Id &op,std::string_view executable,ResourceLedger ledger){
    auto work=ledger.reserve(16*1024*1024);auto encoded=ledger.reserve(compSpanRequestMaximum*2);
    check(fragment<=1 && sha(executable),"Invalid clocked protected selection/executor");
    auto span=prepareClockedProtectedCompSpan(fragment?r.plan().handles().incoming():r.plan().handles().outgoing(),ledger,[]{});
    Json document={{"protocol",compClockedProtectedSpanProtocol},{"operation",op.str()},{"recipeOperation",r.operation().str()},
        {"recipeDigest",r.digest()},{"recipe",r.bytes()},{"recipeExecutor",{{"id",parent.id},{"buildSha256",parent.buildSha256}}},
        {"executor",{{"id",compClockedProtectedSpanProcessorId},{"buildSha256",executable}}},{"fragment",fragment},
        {"protected",clockedProtectedBinding(span,ledger)},{"clock",clockedBinding(span,historyRequired(r))}};
    const auto bytes=document.dump();const auto context=r.plan().handles().outgoing().data().context;work.resize(0);
    return decodeCompSpanRequest(bytes,hash(bytes),context,executable,ledger);
}
CompSpanRequest encodeUniformContextCompSpanRequest(const CompProcessingRecipe &r,const CompRecipeExecutor &parent,unsigned fragment,const Id &op,std::string_view executable,ResourceLedger ledger){
    auto work=ledger.reserve(16*1024*1024);auto encoded=ledger.reserve(compSpanRequestMaximum*2);
    check(fragment<=1 && sha(executable),"Invalid uniform context selection/executor");
    auto span=prepareUniformContextCompSpan(fragment?r.plan().handles().incoming():r.plan().handles().outgoing(),ledger,[]{});
    Json document={{"protocol",compUniformContextSpanProtocol},{"operation",op.str()},{"recipeOperation",r.operation().str()},
        {"recipeDigest",r.digest()},{"recipe",r.bytes()},{"recipeExecutor",{{"id",parent.id},{"buildSha256",parent.buildSha256}}},
        {"executor",{{"id",compUniformContextProcessorId},{"buildSha256",executable}}},{"fragment",fragment},
        {"uniformContext",uniformContextBinding(span,historyRequired(r))}};
    const auto bytes=document.dump();const auto context=r.plan().handles().outgoing().data().context;work.resize(0);
    return decodeCompSpanRequest(bytes,hash(bytes),context,executable,ledger);
}
std::string compSpanIntent(const CompSpanRequest &r,const CompSpanWorkerPolicy &p,ResourceLedger ledger){auto work=ledger.reserve(65536);auto bytes=intent(r,p,ledger).dump();check(bytes.size()<=16384,"Span intent exceeds marker bank");return bytes;}
void verifyCompSpanReady(std::string_view bytes,const CompSpanRequest &r,const CompSpanWorkerPolicy &p,ResourceLedger ledger){auto work=ledger.reserve(256*1024);auto j=parse(bytes,16384);auto expected=intent(r,p,ledger);expected["event"]="ready";check(warp_codec::exact(j,expected),"Span readiness differs from owned request");}
void admitCompSpanSources(const std::filesystem::path &root,const CompSpanRequest &r,const CompSpanWorkerPolicy &p,ResourceLedger ledger,const std::function<void()> &poll){
    policy(p);check(bool(poll),"Missing source admission poll");poll();
    const auto &h=r.recipe().plan().handles();
    // The two sides and their history can repeat the exact same declaration.
    // Retain only completed checks within this one immutable-source admission.
    // A later admission starts empty, including the post-render/post-review pass.
    PayloadCharge seenCharge("Comp source admission declarations",SIZE_MAX);
    seenCharge.add(8,sizeof(const Asset *));
    for(const auto *envelope:{&h.outgoing(),&h.incoming()})
        if(envelope->data().regeneration)seenCharge.add(envelope->data().regeneration->data().assets.size(),sizeof(const Asset *));
    auto seenLease=ledger.reserve(seenCharge.bytes());
    std::vector<const Asset *> seen;seen.reserve(seenCharge.bytes()/sizeof(const Asset *));
    ApprovedMediaRoot approved(root,ledger,{2});
    const auto verify=[&](const Asset &a){poll();
        for(const auto *checked:seen){poll();if(*checked==a)return;}
        auto file=approved.open(a.relativePath,p.maximumSourceBytes);const auto wave=validateApprovedWave(file,waveLimits(a,p.maximumSourceBytes),{}, {},poll);
        check(wave.frames==std::uint64_t(a.frames) && wave.rate==a.sampleRate && wave.channels==a.layout.channels && std::string(wave.sourceSha256.begin(),wave.sourceSha256.end())==a.sha256,"Comp span input changed",ErrorCode::MediaMismatch);file.verifyUnchanged();
        for(const auto &e:a.nativeEvidence){poll();auto f=approved.open(e.relativePath,std::min(p.maximumSourceBytes,e.bytes));check(f.size()==e.bytes,"Span input evidence extent changed");const auto h=f.digest({},poll);check(std::string(h.begin(),h.end())==e.sha256,"Span input evidence changed",ErrorCode::MediaMismatch);f.verifyUnchanged();}
        // Register only after the WAV and every declared companion completed.
        seen.push_back(&a);
    };
    for(const auto *envelope:{&h.outgoing(),&h.incoming()}){const auto &e=envelope->data();verify(e.raw);verify(e.base);verify(e.retainedAsset);if(e.nominalAsset)verify(*e.nominalAsset);
        if(e.regeneration)for(const auto &a:e.regeneration->data().assets)verify(a);}
}
VerifiedCompSpan verifyOwnedCompSpan(const std::filesystem::path &root,const CompSpanRequest &r,const CompSpanWorkerPolicy &p,ResourceLedger ledger,std::stop_token stop){
    policy(p);const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(p.deadlineMilliseconds);
    const auto poll=[&]{check(!stop.stop_requested(),"Span verification canceled",ErrorCode::Canceled);check(std::chrono::steady_clock::now()<deadline,"Span verification deadline exceeded",ErrorCode::ResourceLimit);};poll();
    auto retained=ledger.reserve(128*1024);auto work=ledger.reserve(256*1024);const auto folder=root/"media"/"comp-spans"/r.operation().str();media_io::JobLease lock(folder,false);
    check(lock.status()==media_io::LeaseStatus::Held,"Comp span job active or ownership marker absent");ApprovedMediaRoot approved(root,ledger,{2});
    const auto readMarker=[&](const char *name){auto file=approved.open("media/comp-spans/"+r.operation().str()+"/"+name,16384,stop);std::string bytes(std::size_t(file.size()),'\0');file.readAt(0,bytes,stop);file.verifyUnchanged();return parse(bytes,16384);};
    auto expected=intent(r,p,ledger);check(warp_codec::exact(readMarker("intent.json"),expected),"Span intent differs from original request");
    auto marker=readMarker("complete.json");auto shape=marker;
    for(const char *field:{"complete","writtenFrames","audioSha256","sampleSha256","peakLinear","payloadPeakBytes","durabilityMinimum","memoryMetric"}){check(shape.contains(field),"Span completion field missing");shape.erase(field);}
    check(warp_codec::exact(shape,expected) && marker.at("complete")==true && marker.at("complete").is_boolean(),"Span completion differs from owned request");
    check(warp_codec::unsignedInteger(marker.at("writtenFrames"),1000000000)==std::uint64_t(r.outputFrames()) && marker.at("durabilityMinimum")=="file-flushed" &&
        warp_codec::unsignedInteger(marker.at("payloadPeakBytes"),p.memoryBytes)>0,"Span completion/grant differs");
#ifdef _WIN32
    check(marker.at("memoryMetric")=="process-commit","Wrong native span memory metric");
#else
    check(marker.at("memoryMetric")=="address-space","Wrong native span memory metric");
#endif
    const auto audioSha=marker.at("audioSha256").get<std::string>(),sampleSha=marker.at("sampleSha256").get<std::string>();check(sha(audioSha) && sha(sampleSha),"Invalid span completion hashes");
    check(marker.at("peakLinear").is_number() && std::isfinite(marker.at("peakLinear").get<double>()),"Invalid span completion peak");
    admitCompSpanSources(root,r,p,ledger,poll);Asset output=r.source();output.id=r.operation();output.nativeEvidence.clear();output.frames=r.outputFrames();output.sampleRate=r.outputSampleRate();output.relativePath="media/comp-spans/"+r.operation().str()+"/audio.wav";output.sha256=audioSha;
    auto file=approved.open(output.relativePath,p.maximumOutputBytes,stop);std::array<char,4> magic{};file.readAt(0,magic,stop);check(std::string_view(magic.data(),4)=="RF64","Span result must be RF64");
    auto slabLease=ledger.reserve(512*output.layout.channels*sizeof(float));std::vector<float> slab(512*output.layout.channels);media_io::SampleHash samples;
    const auto wave=validateApprovedWave(file,waveLimits(output,p.maximumOutputBytes),stop,[&](std::uint64_t,std::span<const double> values){poll();check(values.size()<=slab.size(),"Span PCM observer exceeds slab");for(std::size_t i=0;i<values.size();++i)slab[i]=float(values[i]);samples.update({slab.data(),values.size()});},poll);
    check(wave.frames==std::uint64_t(output.frames) && wave.rate==output.sampleRate && wave.channels==output.layout.channels && wave.encoding==WaveEncoding::Float32 &&
        std::string(wave.sourceSha256.begin(),wave.sourceSha256.end())==audioSha && samples.digest()==sampleSha && wave.peak==marker.at("peakLinear").get<double>(),"Span audio differs from completion",ErrorCode::MediaMismatch);
    file.verifyUnchanged();admitCompSpanSources(root,r,p,ledger,poll);check(warp_codec::exact(readMarker("intent.json"),expected),"Span intent changed during verification");check(warp_codec::exact(readMarker("complete.json"),marker),"Span marker changed during verification");poll();return {std::move(retained),std::move(output),r.digest()};
}
} // namespace soundcurrent::daw
