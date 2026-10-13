// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdexcept>
namespace sc_test {
// Construct legacy fixtures from a neutral current writer. Production loading
// remains strict: old schemas reject these keys. Never discard an actual take.
template<class Json> void eraseSchema16FixtureKeys(Json &value) {
    if(value.at("schemaMinor")<18)for(auto &asset:value.at("assets")) {
        if(asset.contains("nativeEvidence") && !asset.at("nativeEvidence").empty())
            throw std::runtime_error("Legacy fixture cannot discard native source evidence");
        asset.erase("nativeEvidence");
    }
    if (value.at("schemaMinor") < 17 && value.contains("loopPlayback")) {
        const auto &loop = value.at("loopPlayback");
        if (loop.at("enabled") != false || loop.at("startFrame") != 0 || loop.at("endFrame") != 0)
            throw std::runtime_error("Legacy fixture cannot discard saved loop intent");
        value.erase("loopPlayback");
    }
    if (value.at("schemaMinor") > 15) return;
    for (auto &track : value.at("tracks")) {
        if (track.contains("takeLanes") && !track.at("takeLanes").empty())
            throw std::runtime_error("Legacy fixture cannot discard retained takes");
        track.erase("takeLanes");
        for (auto &clip : track.at("clips")) {
            if (clip.contains("takeOrigin") && !clip.at("takeOrigin").is_null())
                throw std::runtime_error("Legacy fixture cannot discard comp provenance");
            clip.erase("takeOrigin");
        }
    }
}
}
