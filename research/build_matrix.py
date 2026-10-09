#!/usr/bin/env python3
"""Render the authored workflow register; never infer absence from missing links."""
from pathlib import Path
from urllib.parse import urljoin
from concurrent.futures import ThreadPoolExecutor
import hashlib, json, requests
from bs4 import BeautifulSoup
from reference_labels import neutral_title
ROOT=Path(__file__).resolve().parents[1]

# Group | feature | Reference A topic slug (or ?) | Reference B exact topic (or ?)
# milestone | original acceptance workflow | measurable quality gate | known gap
DATA='''Recording|Multitrack audio capture|recording_clips|Audio Recording|M2|Arm 32 mono tracks; record ten minutes; stop, reopen and verify every take and timestamp.|Zero missing frames in synthetic run; 30-minute native run with zero unreported gaps.|No DAW capture backend exists.
Recording|MIDI record and overdub|recording_clips|MIDI Recording|M3|Record notes, sustain and CC while looping; select replace and overdub separately; reopen and replay.|Recorded events preserve timestamp/order and note-off pairing.|MIDI engine and take semantics are new.
Recording|Input monitoring|intro_to_tracks|Monitoring via Cubase|M2|Switch auto/on/off monitoring while armed, playing and punching; route selected input explicitly.|Measured latency reported within one quantum; no duplicate monitor route.|Native direct monitoring depends on interface capability.
Recording|Punch recording|recording_clips|Stopping Recording Automatically with Punch Out|M2|Punch between non-block-aligned locators with preroll; retain underlying audio and undo.|Punch boundaries within one sample in synthetic fixture.|Need compare every reference record mode.
Recording|Loop recording and take lanes|recording_clips|Lanes, Takes, and Overlapping Events|M2|Record five passes; display, reorder, audition and retain all takes.|No frame duplication or loss at loop wrap.|Audio and MIDI overlapping rules must be explicit.
Recording|Audio comping|working_with_audio_events|Lanes, Takes, and Overlapping Events|M2|Assemble phrases from five takes; adjust transition; reopen; undo entire comp.|No added discontinuity at crossfade; untouched media hashes stable.|Comp model must retain source/take identity.
Recording|Launcher comp recording|recording_launcher_clips|?|M5|Record repeated launcher passes, comp them, then transfer to the arrangement.|Sample-correct transition and identical comp on transfer.|Cubase launcher-equivalent evidence unknown.
Recording|Grouped edits and phase coherence|working_with_note_events|Group Editing Mode|M2|Comp and move an eight-mic drum take together; later warp it with linked markers.|Relative channel offsets remain exact; no introduced phase drift.|Bitwig guide evidence covers layered editing; full grouped workflow requires verification.
Recording|Fades and crossfades|working_with_audio_events|Crossfades|M2|Split two clips, apply editable curved fades/crossfade, trim and undo without changing source.|Gain curve matches specified samples; peak remains predictable.|Fade shapes and defaults require reference comparison.
Recording|Track versions and alternate edits|?|Track Versions|M2|Create two arrangements of one take; switch versions and keep group membership and automation.|Both versions survive save/reopen and undo.|New non-destructive version model.
Recording|AudioWarp and stretch markers|working_with_audio_events|AudioWarp Section|M8|Lock transients to a tempo ramp; change tempo; flatten, undo and restore markers.|Transient drift <1 ms; listening gate Q-STRETCH.|Algorithm and mode parity unproven.
Recording|Segmented pitch and formants|working_with_audio_events|Pitch Editing and Time Correction with VariAudio|M8|Segment a monophonic vocal, edit note pitch/formant/timing, compare to original and undo.|Pitch target within 5 cents on clean fixtures; Q-PITCH listening gate.|Bitwig event pitch is documented; VariAudio-like segmentation remains unverified there.
Recording|Audio alignment|?|Audio Alignment|M8|Align doubled dialogue/vocals, compare time-shift and warp options, preserve unselected regions.|Same-take delay residual <=1 sample; word landmarks <=10 ms.|Automatic speech alignment needs separate implementation.
Recording|Retrospective MIDI capture|?|Inserting a Retrospective Track Recording|M3|Play before pressing record; insert the buffered phrase without stuck notes.|Bounded buffer duration shown; timestamps replay within one sample.|Reference audio retrospective scope unknown.
Recording|Offline processing history|bouncing_to_audio|Direct Offline Processing|M8|Apply, reorder and bypass two destructive-style edits, then recover original without media loss.|Original hash unchanged; render reproducible for deterministic processors.|Bitwig bounce is partial evidence, not the full Cubase process-history workflow.
Mixing|In-process EQ|audio_fx|Channel EQ Section|M1|Record one track; automate gain/frequency; compare playback and export; reopen the project.|Q-EQ; no allocations in prepared processing; live/offline null test.|Studio processor clips internally; DAW wrapper needs float headroom.
Mixing|Buses and group routing|the_mix_view|Group Channel Tracks|M4|Route drums to a bus and vocals to another; nest groups and test solo/mute propagation.|Known unity sum nulls to reference within -120 dBFS.|Graph scheduler and solo policy are new.
Mixing|Pre/post sends|the_mix_view|Pre/Post Fader Sends|M4|Feed a reverb send; alter track fader/pan and toggle pre/post; preserve routes on reload.|Expected levels within 0.01 dB; correct tap timing.|Routing needs explicit tap-point descriptors.
Mixing|Sidechains|audio_fx|Side-Chain Inputs|M4|Drive compressor detector from muted kick; compare internal/external sidechain routing.|Detector/main-path alignment within one sample.|Port-aware sidechain PDC is not in existing engine.
Mixing|VCA controls|?|VCA Fader Track|M4|Link three faders to a VCA; automate VCA plus members; unlink and preserve audible gain.|Summed gain within 0.01 dB; automation persists.|New VCA membership and relative-gain model.
Mixing|External effects routing|audio_fx|External Effects|M4|Patch send/return, measure round trip, print in real time, reopen with missing port.|Impulse alignment within one sample after calibration.|Hardware unavailable for full validation; virtual loop first.
Mixing|External MIDI instruments|intro_to_tracks|External Instruments|M4|Send MIDI to hardware and record its return with saved ports, patch and timing offset.|Measured audio/MIDI alignment <=1 ms under declared setup.|Vendor device maps and real hardware require validation.
Mixing|Recording alignment|the_dashboard|Record - Audio|M2|Capture timestamped loopback through chosen input; apply reported and manual offsets.|Synthetic <=1 sample; physical residual <=1 ms, measured not guessed.|Device latency metadata needs backend verification.
Mixing|Plugin delay compensation|vst_plug-ins|Plug-In Delay Compensation|M4|Join paths with 0/257/2048-sample delays through sends and sidechains; change latency live.|Joins null below -120 dBFS; no unreported latency overflow.|New compiler; host latency changes are asynchronous.
Mixing|Low-latency recording mode|vst_plug-ins|Constrain Delay Compensation|M4|Arm track containing lookahead effects; constrain monitor path; restore on disarm.|Declared monitor latency met; rendered graph remains unchanged.|Monitoring and print policies must be separate.
Mixing|Freeze and bounce|bouncing_to_audio|Freezing Instruments|M6|Freeze a stateful instrument with tail; unload it, reopen, unfreeze and restore automation.|Frozen deterministic render matches; no lost plugin state.|Disk/cache invalidation and nondeterministic plugins complicate null tests.
Mixing|Mix history and channel strip|the_mix_view|MixConsole|M4|Undo a fader gesture, EQ change and route independently; recall channel settings.|Undo order and saved values exact; parameters smooth.|Detailed strip/control inventory pending M0.
Automation|Read/write/touch/latch/cross-over/trim|automation|Automation Modes|M3|Record each punch-out mode across a loop, edit curves and trim, then replay.|Sample offsets exact; specified ramp error <=0.01 dB.|Need verify mode-by-mode against each reference; trim has its own source topic.
Automation|Clip and arrangement automation|automation|Automation Curves|M3|Transfer a clip with curves; override lane while playing; restore read mode.|Base/clip/track precedence consistent after transfer.|Version6 automation-clip changes require release-note tests.
Automation|Expressive notes and MPE|working_with_note_events|Note Expression|M3|Record two simultaneous notes with independent pitch/pressure/timbre and sustain; edit one.|No expression bleed; correct note IDs and release order.|CLAP/VST3 expression mappings need adapters.
Automation|Tempo maps and meter changes|triggering_launcher_clips_0|Editing Tempo and Time Signature|M3|Add step and ramp tempo plus 7/8 change; keep audio/MIDI timing and export map.|Beat/frame conversions <=1 frame; signatures round-trip.|Reference ramp interpolation requires fixture verification.
Automation|Articulations/expression maps|?|Expression Maps|M9|Combine directional and per-note articulations, exclusion groups, keyswitch/CC mappings and attack offsets.|Offsets sample-correct; reset state on seek and loop.|15.x redesigned maps require exhaustive suboption tests.
Automation|Notation editing|?|Basic Concepts@score|M9|Edit voices, tuplets, spelling, transposed parts, percussion, dynamics and lyrics without ruining performed timing.|Score and performed-note identity preserved.|Engraving is a major subsystem, not a piano-roll overlay.
Automation|Notation layout/print|?|Page Formatting and Printing@score|M9|Create conductor score and instrument parts with page breaks, chords and text; print PDF.|No collisions in fixture corpus at configured page size.|Font rights and engraving library decision pending.
Automation|MusicXML/Dorico interchange|?|Importing and Exporting@score|M9|Round-trip voices, tempo, techniques and layouts via MusicXML; report lost properties; assess Dorico files separately.|Semantic fixture diff documented for every supported property.|Dorico native format feasibility unknown, never inferred from MusicXML.
Automation|Controller integration|midi_controllers|MIDI Remote Page|M3|Map keyboard, pad launcher, encoders and motor fader; reconnect device; prevent feedback loops.|Mapping stable after rename/reload; bounded ingress load.|Vendor scripts/protocol permissions need audit.
Automation|Chord/scale assistance|working_with_note_events|Chord Track|M9|Build chords, detect/change scale, guide note edits and make playable generated voicings.|Musical spelling and mapping fixtures pass.|Generic scale snapping is only part of this workflow.
Automation|Logical editors and macros|?|Project Logical Editor|M9|Filter/select/transform notes and project objects; chain undoable commands into a macro.|Deterministic selections and one atomic undo transaction.|Separate MIDI and project predicate/action models required.
Performance|Clip launcher and scenes|the_clip_launcher|?|M5|Play audio/MIDI slots, nested group scenes and stop slots alongside an arrangement.|No incorrect duplicate playback; saved slot state exact.|No verified Cubase full launcher equivalent; U.
Performance|Launch quantization/follow actions|triggering_launcher_clips_0|?|M5|Queue at beat/bar boundaries, legato switch, random/follow actions and alternate launch gestures.|Boundary within one sample; seeded randomness replayable.|6.x gesture and launch option inventory pending.
Performance|Performance capture|recording_launcher_clips|?|M5|Perform scenes with tempo and automation gestures; capture arrangement and replay the performance.|Event timestamps exact; deterministic audio replay nulls.|Nondeterministic device modulation needs captured seeds/state.
Performance|Transport-independent master recording|master_recording|?|M5|Record output while stopping, seeking and restarting transport; import resulting media.|Continuous audio frame sequence despite transport jumps.|Separate capture clock from project transport.
Performance|Hybrid audio/note tracks|intro_to_tracks|?|M5|Place audio and note clips on one track; bounce notes to audio and reverse edit workflow.|Track routing/devices preserve correct signal kinds.|Cubase distinct track types are partial alternatives, not equivalence.
Performance|Device containers|advanced_device_concepts|?|M5|Build serial/parallel/layered/selector devices with macros and sidechains; save as preset.|No implicit stereo loss; routes and IDs survive nested reload.|Containers add latency/scope and preset schema requirements.
Performance|Modulation system|the_unified_modulation_system|Modulators Section|M7|Map LFO/envelope/step/audio follower/macros to devices and track/project parameters.|Explicit automation/modulation precedence; bounded events.|Reference polyphonic versus channel modulation differs.
Performance|Polyphonic modulation|the_unified_modulation_system|?|M7|Play chord with independent per-voice modulators and target compatible hosted instrument.|No cross-voice leakage, bounded CPU/memory for declared voices.|Plugin support is capability-negotiated, not guaranteed.
Performance|Modular sound design|welcome_to_the_grid|?|M7|Patch poly synth, audio effect and note generator with feedback, clocks and voice stealing.|Illegal cycles rejected; legal delayed feedback stable; Q-SYNTH.|Module catalog and UI cannot establish sonic equivalence alone.
Performance|Operators and probabilistic sequencing|operators|Pattern Editor|M5|Use chance/repeats/conditional notes and drum/melodic patterns; expand/bounce to editable results.|Seeded results and event timing match replay.|Vendor condition languages differ; exact behavior needs fixtures.
Plugins|VST3 native hosting|vst_plug-ins|VST Standard|M6|Scan, instantiate effect/instrument, automate, negotiate buses, save state and reopen.|SDK conformance plus Q-HOST corpus; parameter IDs stable.|SDK compile is not host implementation.
Plugins|CLAP native hosting|vst_plug-ins|?|M6|Host CLAP effects/instruments with note expression, modulation, state, tail and offline mode.|Extension thread contracts and timestamp tests pass.|No verified Cubase CLAP support; U.
Plugins|Linux LV2 support|?|?|M6|Discover LV2 RDF/bundles; restore state, atom MIDI/time ports, worker interface and UI.|Lilv fixtures plus no worker/filesystem work in RT.|Linux addition required regardless of vendor baseline support.
Plugins|Discovery and blocklist|vst_plug-in_handling_and_options|VST Plug-in Manager Window|M6|Scan a plugin that hangs or crashes; continue other scans; manually retry blocked item.|Scan timeout and bounded resources; no host crash.|Child scanner, fingerprint cache and compatibility corpus new.
Plugins|Plugin state restoration|vst_plug-ins|Saving VST Presets|M6|Save a plugin using external samples, reopen, duplicate, undo removal and recover state.|Opaque state hashes and dependency loss report exact.|State capture must respect each SDK's thread contract.
Plugins|Window/editor integration|vst_plug-ins|General Plug-in Controls|M6|Open/close/resize HiDPI editor, reopen floating and embedded under X11/Wayland.|No deadlocks; keyboard focus and scaling verified.|Wayland plugins may require XWayland/floating windows.
Plugins|Crash containment and restart|vst_plug-in_handling_and_options|?|M6|Crash or stall one child while recording; keep unaffected tracks, offer restart from checkpoint.|No callback wait; preserved recording; bounded fallback.|Process isolation initially not a security sandbox.
Plugins|Legacy plugin/bit bridging|vst_plug-ins|VST 2 Plug-in Path Settings|M6|Evaluate reference 32-bit/legacy plugin sessions and produce an explicit migration or compatibility path.|Declared format/architecture corpus passes before claiming support.|VST2 licensing and Windows-only bridges unresolved; retained requirement gap.
Monitoring|Control room monitor sets|master_track_routing|Control Room|M4|Switch speaker sets, dim/mute/mono/phones and compare sources without changing master print.|Master export hash unchanged by all monitor-only controls.|Monitor buses and device configuration are new.
Monitoring|Cue mixes and talkback|master_track_routing|Setting up a Cue Mix|M4|Create four independent headphone mixes; route gated talkback, dim monitors and record.|Talkback absent from master render; no feedback route.|Ambisonics cue limitations differ in Cubase; investigate actual use cases.
Monitoring|Monitoring-only room correction|audio_fx|Control Room - Inserts Tab|M4|Load speaker/room EQ on monitor bus; bypass it, reopen and export master.|Corrected monitoring changes; export unchanged bit-for-bit.|Approximate profiles do not replace measured room response.
Monitoring|Peak/RMS/spectrum/phase metering|the_mix_view|Metering and Loudness|M4|Observe calibrated sine, impulses and anti-phase stereo at several zoom/layout sizes.|Levels <=0.1 dB error; UI refresh independent of DSP.|Analysis worker and stereo/multichannel views need adaptation.
Monitoring|Loudness and true peak|?|Loudness Measurement|M4|Measure short-term/integrated/gated loudness and intersample peak; export analysis report.|Q-LOUDNESS fixtures within 0.1 LU and 0.1 dBTP.|Choose tested meter library; streaming allocation audit pending.
Content|Instrument families|device_descriptions|VST Instruments|M7|Create subtractive, FM, wavetable, physical-model, organ and drum parts using bundled instruments.|Q-SYNTH; rights and per-family coverage audit C-CONTENT.|Exact catalog needs enumeration; no vendor code/assets copied.
Content|Sampler and multisample instruments|device_descriptions|Sampler Tracks|M7|Map velocity/key zones and round robins; loop/slice/stretch/granular/spectral playback; relocate samples.|Boundary clicks controlled; samples and mappings portable.|Bitwig6.1 expanded Sampler is a distinct baseline delta.
Content|Drum machine and pattern tools|percussion|Drum Tracks|M5|Build a layered kit with choke groups and melodic/drum patterns; edit velocity and swing.|Timing deterministic and mapped note identity persistent.|Original kits require licensed samples.
Content|Effect families|audio_fx|Audio Effects|M7|Complete a mix with dynamics/EQ/delay/reverb/modulation/distortion/convolution/pitch and mastering tools.|Q-DSP and listening comparisons per category.|Existing delay/reverb is only a starting point; cannot claim catalog parity.
Content|Browser and audition|browsers|MediaBay|M7|Tag/search media, presets and devices; tempo-sync preview; insert and relocate content.|Search target <200 ms for indexed 100k items; audio callback unaffected.|Cold indexing asynchronous; benchmark hardware to be declared.
Content|Preset and content coverage|device_descriptions|Instrument Presets Results Browser|M11|Audition original presets across reference musical categories and build three complete demo projects.|C-CONTENT coverage review and complete rights inventory.|Number of preset names is not content equivalence.
Content|Vocal synthesis/companion tools|?|New Features|M11|Inventory bundled companion and beta tools; define original vocal synthesis/editing and mastering workflows.|Separate quality/content benchmarks; no proprietary asset copying.|Exact Pro entitlements and beta behavior need reference license verification.
Immersive|Surround and arbitrary layouts|master_track_routing|Surround Sound|M10|Mix mono/stereo into 5.1/7.1.4 and a discrete 256-channel fixture; map/export every channel.|Channel identity and impulse energy preserved; panning test corpus.|256-channel EQ processing alone is not surround mixing.
Immersive|Ambisonics encode/decode|?|Ambisonics Mixes|M10|Encode/rotate/decode 1st-4th order ACN/SN3D, convert FuMa, export AmbiX and binaural monitor.|Q-SPATIAL; known spherical harmonics tests and metadata round-trip.|Cubase overview lists fewer orders than dedicated topic; use dedicated 4OA evidence and confirm live.
Immersive|Head tracking and VR monitoring|?|Head-Tracking Data from VR Controller Devices|M10|Rotate monitor orientation from a controller and test head-locked source; export unaffected.|Bounded tracking latency; rendered mix unaffected by monitor head pose.|Physical headset validation deferred, synthetic tracker required.
Immersive|Object audio and ADM delivery|?|Exporting ADM Files|M10|Create bed/objects, animate metadata and export/reimport ADM BWF in permitted tools.|Object IDs/timing/routing exact; format validator gate.|Dolby branding/renderer licensing and platform availability unresolved; no compliant claim yet.
Video|Video playback and edits|?|Video|M10|Import rational-frame-rate video, offset and scrub while recording; replace soundtrack and export.|AV offset <=1 video frame; no record callback stalls.|Codec/render/backend scope needs dedicated inventory.
Video|Timecode and synchronization|?|Timecode Formats|M10|Chase MTC/LTC with preroll, drop-frame timecode and discontinuity; retain project offset.|No cumulative drift over one hour fixture; correct drop-frame mapping.|Hardware slave-clock control requires physical qualification.
Video|Network/device synchronization|?|VST System Link|M10|Define licensed equivalent multi-workstation sync/routing workflow; measure clock drift and failover.|Explicit jitter/clock budget and no silently duplicated transport events.|Proprietary protocol compatibility unknown; functional alternative must be demonstrated.
Analysis|Stem separation|?|Stem Separation|M8|Separate voice/drums/bass/other, optionally combine/mute source, undo and reopen results.|Q-STEMS; deterministic provenance and original-media hash.|Model/runtime licensing and reference sonic quality unproven.
Delivery|Offline/realtime export WAV|bouncing_to_audio|Export Audio Mixdown|M1|Export selected range through EQ, include declared tail and reopen WAV with rate/layout metadata.|Frame count exact; deterministic live/offline null test.|Synthetic probe only; project slice not implemented.
Delivery|Batch/stem export and queues|working_with_projects_and_exporting|Export Queue Section|M8|Queue buses/tracks/stems with naming, ranges and tail options; cancel and resume safely.|No partial file published; metadata/range deterministic.|New job/transaction subsystem.
Delivery|Formats/dither/resample|working_with_projects_and_exporting|File Formats|M8|Export PCM16/24/float, RF64/BWF/FLAC/AIFF and evaluated compressed formats; compare metadata.|Q-RESAMPLE; seeded dither and format boundary tests.|Codecs/build licenses per format; source conversion never implicit.
Delivery|MIDI file interchange|working_with_projects_and_exporting|Exporting Instrument Tracks as MIDI Files|M3|Import/export SMF tempo/signatures/controllers; retain notation and expression through loss reporting.|No event timing loss for supported subset; explicit unsupported fields.|MPE/native note expression need stated interchange limits.
Delivery|DAWproject interchange|working_with_projects_and_exporting|DAWproject Files|M8|Round-trip audio/MIDI/tempo/routing/automation/plugin states via fixtures from both references.|Semantic diff + per-property loss report; samples portable.|Open format does not imply native project compatibility.
Delivery|AAF and OMF exchange|?|AAF Files|M10|Import/export broadcast session with media offsets, fades and linked/embedded assets; test OMF separately.|Frame/sample offsets exact for declared subset.|AAF/OMF dependency/specification and rights review pending.
Reliability|Portable project/media pool|working_with_projects_and_exporting|Self-Contained Projects|M2|Collect/copy assets, move project to another path/machine, verify hashes and reopen.|No absolute-only references; no duplicated source replacement.|New session directory and asset manifest.
Reliability|Missing media relink|working_with_projects_and_exporting|Finding Missing Files|M2|Move one asset; open placeholder, find exact hash and relink; preserve edits and takes.|Wrong same-named media never silently accepted.|Fallback matching requires visible user selection.
Reliability|Missing plugins/ports|working_with_projects_and_exporting|Re-Routing Missing Ports|M6|Open without plugin/device; retain state, IDs/routes/automation; install/reconnect and restore.|Round-trip unopened state without information loss.|External-device missing-plugin topic is not general plugin evidence.
Reliability|Undo/redo|working_with_audio_events|Edit History|M1|Undo a control gesture, split, preset, route and take edit; redo after reopen only if history policy supports it.|Transactions deterministic; no orphan media; RT unaffected.|First slice saves current state; persistent history scope explicit later.
Reliability|Autosave and snapshots|working_with_projects_and_exporting|Auto Save|M2|Change session during record; create timed autosave and reopen newest valid snapshot.|No callback IO; retain previous valid generation after failed save.|Media capture durability separate from project autosave.
Reliability|Interrupted-recording recovery|?|?|M2|Kill writer/app during capture and inject disk-full; recover valid prefix and show gaps.|Recover durable frame extent within one-second commit policy.|Both vendors' exact guarantees unknown; explicit SoundCurrent requirement.
Reliability|Versioned migrations|?|Updating from a Previous Version of Cubase|M2|Open old schema, migrate a copy, preserve original and unknown fields; refuse unsupported major version.|Fixture golden semantic diff; old project untouched.|Cubase preferences migration is partial evidence, not project schema guarantee.
Reliability|Backups and archive restore|working_with_projects_and_exporting|Backups|M2|Archive project/media/state, restore on clean workspace and verify all dependency warnings.|Hash-verified restore; media referenced by undo not prematurely removed.|Backup retention and garbage collection need design.
Compatibility|Native BWPROJECT/CPR access|working_with_projects_and_exporting|Project Files|M11|Obtain permitted specifications and reference projects; inventory unsupported data; round-trip only proven fields.|N-NATIVE corpus with audible/state differences reported.|No documented complete public native specs established; no native compatibility claimed.
Quality|Load/performance and accessibility|working_on_a_tablet_computer|Audio Performance Monitor Panel|M11|Run recording/live/modular/plugin workloads; operate main editors by keyboard at 1280x720 and HiDPI.|Published hardware/quantum/track budgets; no unexplained xruns; accessible focus and scaling.|Reference performance is not measured yet; new product cannot promise same capacity.
'''

def main():
    sources=json.loads((ROOT/'research/sources.json').read_text())
    cindex=json.loads((ROOT/'.cache/sources/cubase-links.json').read_text())
    scoreurl='https://www.steinberg.help/r/cubase-pro/cubasescore/15.0/en'
    sr=next(r for r in sources if r['url']==scoreurl)
    soup=BeautifulSoup((ROOT/'.cache/sources'/f"{sr['id']}.html").read_text(),'html.parser')
    score=[(a.get_text(' ',strip=True),urljoin(scoreurl,a['href'])) for a in soup.find_all('a',href=True)]
    chosen={};rows=[]
    for i,line in enumerate(DATA.strip().splitlines(),1):
        group,feature,b,c,m,workflow,q,gap=line.split('|')
        bu='https://www.bitwig.com/userguide/latest/'+b+'/' if b!='?' else None
        cu=None
        if c!='?':
            table=score if c.endswith('@score') else cindex
            title=c.removesuffix('@score')
            title={'Channel EQ Section':'Equalizer Section', 'Finding Missing Files':'Locating Missing Files',
                   'Edit History':'Edit History Dialog', 'Backups':'Back up Project Options Dialog',
                   'Project Files':'Project Files and Project Locations'}.get(title,title)
            matches=[u for t,u in table if t.replace('\u00a0',' ')==title]
            if matches: cu=matches[0]
        for url,title in [(bu,b),(cu,c)]:
            if url: chosen[url]=title
        rows.append(dict(id=f'P{i:03}',group=group,feature=feature,milestone=m,acceptance=workflow,
                         quality=q,known_gap=neutral_title(gap),reference_a_url=bu,reference_b_url=cu,
                         reference_a_evidence='P' if bu else 'U',reference_b_evidence='D' if cu else 'U',
                         implementation='planned; not implemented',F='unverified',Q='unverified',
                         C='coverage pending' if group=='Content' else 'not applicable',
                         N='unverified' if group=='Compatibility' else 'not implied'))
    urls={r['url'].rstrip('/'):r for r in sources}
    missing=[(u,t) for u,t in chosen.items() if u.rstrip('/') not in urls]
    def collect(pair):
        url,title=pair;key=hashlib.sha256(url.encode()).hexdigest()[:16]
        r=dict(id=key,title=neutral_title(title),url=url,accessed='2026-10-05')
        try:
            response=requests.get(url,timeout=35);response.raise_for_status();raw=response.content
            r.update(retrieved=True,sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw))
            (ROOT/'.cache/sources'/f'{key}.html').write_bytes(raw)
            (ROOT/'.cache/sources'/f'{key}.txt').write_text(BeautifulSoup(raw,'html.parser').get_text(' ',strip=True))
        except Exception as e:r.update(retrieved=False,error=str(e))
        return r
    with ThreadPoolExecutor(max_workers=8) as pool:sources+=list(pool.map(collect,missing))
    # Retry transient failures without advancing already-frozen bytes.
    retry=[(r['url'],r['title']) for r in sources if not r.get('retrieved') and r['url'] in chosen]
    for pair in retry:
        replacement=collect(pair)
        sources=[replacement if r['url']==pair[0] else r for r in sources]
    urls={r['url'].rstrip('/'):r for r in sources}
    for row in rows:
        for vendor in ['reference_a','reference_b']:
            url=row[vendor+'_url'];ref=urls.get(url.rstrip('/')) if url else None
            row[vendor+'_source_id']=ref['id'] if ref else None
            if ref and not ref.get('retrieved'):row[vendor+'_evidence']='U'
        # Some related topics document only a narrower alternative.
        if row['feature'] in ['Segmented pitch and formants','Grouped edits and phase coherence','Offline processing history','MusicXML/Dorico interchange','Vocal synthesis/companion tools','Versioned migrations','Native BWPROJECT/CPR access']:
            row['reference_limit']='Topic supports the feature family; detailed workflow equivalence is unresolved.'
    (ROOT/'research/sources.json').write_text(json.dumps(sources,indent=2)+'\n')
    (ROOT/'research/parity.json').write_text(json.dumps({'baseline':'SC-DAW-BASELINE-2026-10-05','rows':rows},indent=2)+'\n')
    preamble='''# Traceable parity matrix and acceptance register

Baseline: full **Reference A 6.1.3** + **Reference B 15.0.30**, frozen 2026-10-05. This is an authored acceptance register, not a claim of implemented parity.

**D** = a directly linked Reference B 15 topic documents the feature family. **P** = Reference A documentation evidence is provisional because the general guide is 5.3; 6.x changes must be checked against the frozen changelog/PDF and licensed application. **U** = uncertain, undocumented here, or the examined source only offers an alternative. U never means absent. Desired acceptance behavior may exceed what the cited introductory topic proves. Detailed suboptions and defaults require M0 decomposition and hands-on fixtures. Every row currently has an implementation gap.

The acceptance workflows below are original specifications for SoundCurrent. Their numerical thresholds are proposed product gates, not published claims about either reference's performance. Quality gate IDs are defined in `docs/04-roadmap.md`. Milestones are dependencies, not feature exclusions.

Axes are separately stored per row in [parity.json](../research/parity.json): F/Q = unverified for all; C = pending for content rows and otherwise not applicable; N = unverified for native formats and never implied by other rows. The [baseline](00-scope-baseline.md) defines them. Source IDs map to retrieved hashes in [sources.json](../research/sources.json).

'''
    out=[preamble]
    current=None
    for row in rows:
        if row['group']!=current:
            current=row['group'];out+=['\n## '+current+'\n\n','| ID / target | Reference A evidence | Reference B evidence | Acceptance workflow | Quality gate | Known gap |\n','|---|---|---|---|---|---|\n']
        refs=[]
        for v in ['reference_a','reference_b']:
            u=row[v+'_url'];state=row[v+'_evidence']
            refs.append(f'[{state}: topic]({u})' if u else 'U — not established')
        out.append(f"| {row['id']} **{row['feature']}** ({row['milestone']}) | {refs[0]} | {refs[1]} | {row['acceptance']} | {row['quality']} | {row['known_gap']} |\n")
    out+=['''
## Baseline deltas and unresolved breadth

- **Reference A 6.0/6.1**: the frozen [6.1/6.0 document](https://downloads.bitwig.com/6.1/Release-Notes-6.1.pdf) describes Sampler slicing/spectral changes, editing and automation work. Sampler, automation and performance families require release-specific subtests, not just 5.3 chapter assertions. Row IDs are stable once approved.
- **Reference B 15**: separately inventory its redesigned expression-map options, extended modulation/pattern tools, stem separation and bundled/beta/companion tools from [New Features](https://www.steinberg.help/r/cubase-pro/15.0/en) and licensed Pro installation. Main and Score documentation are separate. Reference B webhelp sometimes contains inherited overview inconsistencies (Ambisonics order count); use dedicated topic evidence and verify behavior.
- No negative vendor assertions are made for LV2, CLAP in Reference B, notation in Reference A, proprietary formats or interruption recovery where evidence is U.
- VST2/32-bit compatibility, object-audio/Dolby deliverables, Dorico/native projects, vendor controllers and bundled companion applications remain consequential rights/feasibility gaps. An alternative workflow is a proposed solution, not an automatic parity pass.
- Before an F/Q/C/N claim, expand every family into observed suboptions and failure cases, run it in the frozen reference and SoundCurrent, keep evidence and record exceptions. All unknowns must be resolved or prominently accepted as known product gaps; marketing cannot silently drop them.
''']
    (ROOT/'docs/01-parity-matrix.md').write_text(''.join(out))
    print('Authored rows:',len(rows),'new sources:',len(missing),'retrieval failures:',sum(not r['retrieved'] for r in sources))

if __name__=='__main__':main()
