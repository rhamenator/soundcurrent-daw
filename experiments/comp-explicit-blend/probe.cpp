// SPDX-License-Identifier: GPL-3.0-only
// Reuse unchanged owned R3 observations. No rerender or project adoption.
#include <soundcurrent/comp_handle_pcm.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
void require(bool v,const char *m){if(!v)throw std::runtime_error(m);}
std::filesystem::path path(const char *arg){const std::string bytes=arg;return std::filesystem::path(std::u8string(bytes.begin(),bytes.end()));}
std::vector<float> read(const std::filesystem::path &p,std::size_t frames){
    require(frames && frames<=100000,"Input PCM exceeds experiment grant");const auto bytes=frames*2*sizeof(float);
    require(std::filesystem::file_size(p)==bytes,"Input PCM extent differs");std::vector<float> v(frames*2);
    std::ifstream in(p,std::ios::binary);in.read(reinterpret_cast<char *>(v.data()),std::streamsize(bytes));require(bool(in),"PCM read failed");return v;
}
void write(const std::filesystem::path &p,const std::vector<float> &v){std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char *>(v.data()),std::streamsize(v.size()*sizeof(float)));require(bool(out),"PCM write failed");}
CompHandlePcmRead reader(const std::vector<float> &v){return [&v](Frame at,std::span<float> out){require(at>=0 && std::size_t(at)*2<=v.size() && out.size()<=v.size()-std::size_t(at)*2,"Blend PCM read exceeded source");std::copy_n(v.begin()+std::size_t(at)*2,out.size(),out.begin());};}
Json metrics(const std::optional<CompHandleJoinMetrics> &m){require(bool(m) && bool(m->slopeDeviationPeak),"Missing join measurements");return Json{{"stepPeak",m->stepPeak},{"slopeDeviationPeak",*m->slopeDeviationPeak}};}
}
int main(int argc,char **argv){try{
    static_assert(std::endian::native==std::endian::little,"Owned PCM fixture is little endian");
    require(argc==3,"Supply retained input and new output directories");const auto input=path(argv[1]),root=path(argv[2]);require(std::filesystem::create_directory(root),"Output directory exists");
    Json bank;std::ifstream in(input/"report.json");in>>bank;
    require(bank.at("format")=="sc-comp-edge-assembly-probe-v1" && bank.at("cases").size()==12,"Unknown retained input bank");
    ResourceLedger ledger(4*1024*1024,"Explicit blend probe");Json rows=Json::array();unsigned assemblies=0;
    for(std::size_t n=0;n<12;++n){const auto &c=bank["cases"][n];const auto name=c.at("case").get<std::string>();require(name=="case-"+std::to_string(n),"Unexpected input case path");
        const auto frames=c.at("nominalOutputFrames").get<Frame>(),edge=c.at("edgeFrames").get<Frame>(),ctx=c.at("contextOutputFrames").get<Frame>();require(edge==26 && c.at("channels")==2,"Unexpected fixture layout");
        const auto current=read(input/name/"nominal.pcm",std::size_t(frames));const auto prefix=read(input/name/"prefix-context.pcm",std::size_t(ctx));const auto suffix=read(input/name/"suffix-context.pcm",std::size_t(ctx));
        const auto before=c.at("prefixCropFrame").get<Frame>();
        for(const Frame width:{26,128,1024}){require(width*2<frames && before+edge+width<=ctx && frames+edge<=ctx,"Blend exceeds actual context");
            std::vector<float> candidate(std::size_t(frames+2*edge)*2,0);
            std::copy_n(prefix.begin()+std::size_t(before)*2,std::size_t(edge+width)*2,candidate.begin());
            std::copy_n(suffix.begin()+std::size_t(frames-width)*2,std::size_t(width+edge)*2,candidate.begin()+std::size_t(edge+frames-width)*2);
            const CompHandlePcmBlends blend{CompHandleInnerBlend{width,0,width},CompHandleInnerBlend{width,frames-width,frames}};
            std::vector<float> output;const auto result=blendCompHandlePcm({2,127,frames,edge,edge},blend,reader(current),reader(candidate),
                [&](auto values){output.insert(output.end(),values.begin(),values.end());},ledger,[]{});++assemblies;
            std::vector<float> other;blendCompHandlePcm({2,511,frames,edge,edge},blend,reader(current),reader(candidate),[&](auto values){other.insert(other.end(),values.begin(),values.end());},ledger,[]{});
            require(other==output && !ledger.usage().reservedBytes,"Blend partitions or resource retirement differ");
            for(Frame f=width;f<frames-width;++f)for(unsigned ch=0;ch<2;++ch)require(std::bit_cast<std::uint32_t>(output[std::size_t(f+edge)*2+ch])==std::bit_cast<std::uint32_t>(current[std::size_t(f)*2+ch]),"Blend changed an unpermitted sample");
            const auto file=name+"-width-"+std::to_string(width)+".pcm";write(root/file,output);
            rows.push_back(Json{{"case",name},{"width",width},{"channels",2},{"rate",48000},{"currentFrames",frames},{"edgeFrames",edge},{"outputFrames",result.writtenFrames},
                {"prefixPermission",{0,width}},{"suffixPermission",{frames-width,frames}},{"pcm",file},{"prefixJoin",metrics(result.prefixJoin)},{"suffixJoin",metrics(result.suffixJoin)},{"floatPeak",result.floatPeak}});
        }
    }
    Json report{{"format","sc-explicit-comp-blend-probe-v1"},{"curve","complementary-quintic-v1"},{"assemblies",assemblies},{"additionalR3Renders",0},{"rows",rows},{"qualityAdmission",false},{"modelVerifierChanged",false}};
    std::ofstream out(root/"report.json");out<<report.dump(2)<<'\n';require(bool(out),"Report write failed");std::cout<<Json{{"assemblies",assemblies},{"additionalR3Renders",0},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
