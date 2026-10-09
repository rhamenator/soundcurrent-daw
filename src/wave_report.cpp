// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wave_report.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
namespace soundcurrent::daw {
namespace {
void check(bool good,const char *text) {if (!good) throw ProjectError(ErrorCode::InvalidState,text);}
void poll(std::stop_token stop) {if (stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Wave report decoding canceled");}
}
WaveCheckReport decodeWaveCheckReport(OwnedInspectionProtocol protocol,std::string_view expected,
    std::size_t pid,std::uint64_t maximum,ResourceLedger memory,ResourceLease values,ResourceLease parser,std::stop_token stop) {
    poll(stop);
    check(protocol.ownedBy(memory) && memory.owns(values) && memory.owns(parser),"Unadmitted WAVE report scope");
    check(protocol.bytes().size()<=waveReportMaximumBytes && values.bytes()>=waveReportValueCharge &&
          parser.bytes()>=waveReportParserCharge,"Insufficient WAVE report grants");
    check(pid && maximum && !expected.empty() && expected.size()<=1024 && validUtf8(expected),"Invalid WAVE report context");
    for (const unsigned char c:protocol.bytes()) check(c>0 && c<128,"WAVE report must be bounded ASCII JSON");
    constexpr std::array<std::string_view,18> fields{"protocol","complete","relative","sourceBytes","sourceSha256",
        "frames","decodedFrames","rateHz","channels","containerId","extensible","encodingId","bitsPerSample",
        "channelMask","peakLinear","bytesRead","ioOperations","workerPid"};
    std::array<bool,fields.size()> seen{};
    try {
        const auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &parsed) {
            poll(stop);check(depth<=1,"Nested WAVE report refused");
            check(event!=nlohmann::json::parse_event_t::array_start,"Array WAVE report refused");
            if (event==nlohmann::json::parse_event_t::key) {
                const auto key=parsed.get<std::string>();const auto it=std::find(fields.begin(),fields.end(),key);
                check(it!=fields.end(),"Unknown WAVE report field");const auto index=static_cast<std::size_t>(it-fields.begin());
                check(!seen[index],"Duplicate WAVE report field");seen[index]=true;
            }
            return true;
        };
        const auto json=nlohmann::json::parse(protocol.bytes(),callback);
        check(json.is_object() && json.size()==fields.size() && std::all_of(seen.begin(),seen.end(),[](bool b){return b;}),"Incomplete WAVE report");
        auto integer=[&](const char *key,std::uint64_t upper) {
            const auto &v=json.at(key);check(v.is_number_integer(),"Non-integral WAVE report value");
            check(!v.is_number_integer() || v.is_number_unsigned() || v.get<std::int64_t>()>=0,"Negative WAVE report count");
            const auto n=v.get<std::uint64_t>();check(n<=upper,"WAVE report count exceeds policy");return n;
        };
        check(json.at("protocol")=="sc-approved-wave-validation-v2" && json.at("complete").is_boolean() &&
              json.at("complete").get<bool>(),"Unsupported/incomplete WAVE report protocol");
        check(integer("workerPid",std::numeric_limits<std::size_t>::max())==pid,"WAVE report child PID mismatch");
        check(json.at("relative").is_string() && json.at("relative")==expected,"WAVE report reference mismatch");
        WaveCheckReport result(std::move(values),std::move(protocol));result.relative_=expected;result.pid_=pid;
        auto &a=result.audio_;const WaveValidationLimits limits;
        a.sourceBytes=integer("sourceBytes",std::min<std::uint64_t>(maximum,UINT32_MAX+8ULL));
        check(a.sourceBytes>=44,"WAVE report source extent too short");
        a.frames=integer("frames",limits.maximumFrames);a.decodedFrames=integer("decodedFrames",limits.maximumFrames);
        check(a.frames==a.decodedFrames,"WAVE report incomplete frame decode");
        a.rate=static_cast<std::uint32_t>(integer("rateHz",INT32_MAX));a.channels=static_cast<std::uint32_t>(integer("channels",limits.maximumChannels));
        check(a.rate && a.channels,"WAVE report rate/channels empty");
        const auto encoding=integer("encodingId",6);check(encoding>=1,"Unknown WAVE encoding");a.encoding=static_cast<WaveEncoding>(encoding);
        constexpr std::array<unsigned,6> widths{8,16,24,32,32,64};a.bitsPerSample=static_cast<std::uint32_t>(integer("bitsPerSample",64));
        check(a.bitsPerSample==widths[static_cast<std::size_t>(encoding-1)],"WAVE report precision mismatch");
        check(a.frames<=(a.sourceBytes-44)/(a.channels*(a.bitsPerSample/8)),"WAVE frames exceed original byte extent");
        check(json.at("containerId")=="riff" || json.at("containerId")=="rifx","Unknown WAVE container");a.bigEndian=json.at("containerId")=="rifx";
        check(json.at("extensible").is_boolean(),"Invalid WAVE extensible flag");a.extensible=json.at("extensible").get<bool>();
        a.channelMask=static_cast<std::uint32_t>(integer("channelMask",UINT32_MAX));
        check((!a.extensible && a.channelMask==0) || (a.extensible && !a.bigEndian && a.sourceBytes>=68 &&
            (!a.channelMask || std::popcount(a.channelMask)==static_cast<int>(a.channels))),"WAVE report layout mismatch");
        check(json.at("peakLinear").is_number(),"Invalid WAVE peak type");a.peak=json.at("peakLinear").get<double>();
        check(std::isfinite(a.peak) && a.peak>=0 && (a.frames || a.peak==0) && (encoding>=5 || a.peak<=1),"Invalid WAVE peak/headroom");
        a.bytesRead=integer("bytesRead",limits.maximumBytesRead);a.ioOperations=integer("ioOperations",limits.maximumIoOperations);
        check(a.bytesRead>=a.sourceBytes && a.ioOperations,"Incomplete WAVE read/hash work");
        check(json.at("sourceSha256").is_string(),"Invalid WAVE digest type");const auto hash=json.at("sourceSha256").get<std::string>();
        check(hash.size()==64 && std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}),"Invalid WAVE digest");
        std::copy(hash.begin(),hash.end(),a.sourceSha256.begin());poll(stop);return result;
    } catch (const nlohmann::json::exception &) {throw ProjectError(ErrorCode::InvalidState,"Malformed WAVE report");}
}
} // namespace soundcurrent::daw
