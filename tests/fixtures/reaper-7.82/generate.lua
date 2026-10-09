-- SPDX-License-Identifier: GPL-3.0-only
-- Original SoundCurrent fixture authoring through public ReaScript APIs.
-- Run only in the isolated /lab workspace described by README.md.
assert(reaper.GetAppVersion() == "7.82/linux-x86_64", "Wrong native writer")
assert(reaper.GetPlayState() == 0, "Unexpected playback")
local root = "/lab/corpus/"
local function json(v)
  local t = type(v)
  if t == "string" then
    return '"' .. v:gsub('[%z\1-\31\\"]', function(c)
      if c == '"' then return '\\"' end
      if c == '\\' then return '\\\\' end
      return string.format('\\u%04x', c:byte())
    end) .. '"'
  elseif t == "number" then
    assert(v == v and math.abs(v) < math.huge, "Nonfinite observation")
    return string.format("%.17g", v)
  elseif t == "boolean" then return tostring(v)
  elseif t == "table" then
    local parts = {}
    if v.array then
      for _, x in ipairs(v.array) do parts[#parts+1] = json(x) end
      return '[' .. table.concat(parts, ',') .. ']'
    end
    local keys = {}
    for k in pairs(v) do keys[#keys+1] = k end
    table.sort(keys)
    for _, k in ipairs(keys) do parts[#parts+1] = json(k) .. ':' .. json(v[k]) end
    return '{' .. table.concat(parts, ',') .. '}'
  end
  error("Unqualified JSON value")
end
local function output(name, data)
  local f = assert(io.open(root .. name, "wb"))
  assert(f:write(json(data), "\n")); assert(f:close())
end
local function reset()
  reaper.Main_openProject("noprompt:/lab/blank.rpp")
  assert(reaper.CountTracks(0) == 0 and reaper.GetPlayState() == 0)
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE", 48000, true)
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
end
local function track(name, gain, pan)
  local index = reaper.CountTracks(0)
  reaper.InsertTrackAtIndex(index, false) -- no default FX/envelopes
  local t = assert(reaper.GetTrack(0, index))
  assert(reaper.GetSetMediaTrackInfo_String(t, "P_NAME", name, true))
  assert(reaper.SetMediaTrackInfo_Value(t, "D_VOL", gain or 1))
  assert(reaper.SetMediaTrackInfo_Value(t, "D_PAN", pan or 0))
  return t
end
local function clip(t, media, position, offset, length, extra)
  local item = assert(reaper.AddMediaItemToTrack(t))
  local take = assert(reaper.AddTakeToMediaItem(item))
  local source = assert(reaper.PCM_Source_CreateFromFile(root .. "media/" .. media))
  local previous = reaper.GetMediaItemTake_Source(take)
  assert(reaper.SetMediaItemTake_Source(take, source))
  -- A new external source becomes take-owned. The documented setter leaves
  -- the previous source to the caller, if this newly added take had one.
  if previous then reaper.PCM_Source_Destroy(previous) end
  assert(reaper.SetMediaItemInfo_Value(item, "D_POSITION", position))
  assert(reaper.SetMediaItemInfo_Value(item, "D_LENGTH", length))
  assert(reaper.SetMediaItemInfo_Value(item, "B_LOOPSRC", 0))
  assert(reaper.SetMediaItemTakeInfo_Value(take, "D_STARTOFFS", offset))
  assert(reaper.GetSetMediaItemTakeInfo_String(take, "P_NAME", "Prise \"α\" – Запись", true))
  if extra then
    for key, value in pairs(extra.item or {}) do
      assert(reaper.SetMediaItemInfo_Value(item, key, value))
    end
    for key, value in pairs(extra.take or {}) do
      assert(reaper.SetMediaItemTakeInfo_Value(take, key, value))
    end
  end
  return item, take
end
local function observe()
  local tracks = {}
  for i = 0, reaper.CountTracks(0)-1 do
    local t = assert(reaper.GetTrack(0, i))
    local _, name = reaper.GetSetMediaTrackInfo_String(t, "P_NAME", "", false)
    local items = {}
    for n = 0, reaper.CountTrackMediaItems(t)-1 do
      local item = assert(reaper.GetTrackMediaItem(t, n))
      local take = assert(reaper.GetActiveTake(item))
      local src = assert(reaper.GetMediaItemTake_Source(take))
      local _, takeName = reaper.GetSetMediaItemTakeInfo_String(take, "P_NAME", "", false)
      local row = {name=takeName, position=reaper.GetMediaItemInfo_Value(item,"D_POSITION"),
        length=reaper.GetMediaItemInfo_Value(item,"D_LENGTH"),
        fadeIn=reaper.GetMediaItemInfo_Value(item,"D_FADEINLEN"),
        fadeOut=reaper.GetMediaItemInfo_Value(item,"D_FADEOUTLEN"),
        gain=reaper.GetMediaItemInfo_Value(item,"D_VOL"),
        sourceOffset=reaper.GetMediaItemTakeInfo_Value(take,"D_STARTOFFS"),
        takeGain=reaper.GetMediaItemTakeInfo_Value(take,"D_VOL"),
        takePan=reaper.GetMediaItemTakeInfo_Value(take,"D_PAN"),
        playRate=reaper.GetMediaItemTakeInfo_Value(take,"D_PLAYRATE"),
        pitch=reaper.GetMediaItemTakeInfo_Value(take,"D_PITCH"),
        sourceChannels=reaper.GetMediaSourceNumChannels(src),
        sourceSampleRate=reaper.GetMediaSourceSampleRate(src),
        sourceFile=reaper.GetMediaSourceFileName(src, ""),
        midi=reaper.TakeIsMIDI(take)}
      if row.midi then
        local _, notes, cc, text = reaper.MIDI_CountEvts(take)
        row.midiNotes=notes;row.midiControllers=cc;row.midiTextEvents=text
      end
      items[#items+1] = row
    end
    tracks[#tracks+1] = {name=name, guid=reaper.GetTrackGUID(t),
      gain=reaper.GetMediaTrackInfo_Value(t,"D_VOL"),
      pan=reaper.GetMediaTrackInfo_Value(t,"D_PAN"),
      channels=reaper.GetMediaTrackInfo_Value(t,"I_NCHAN"),
      sends=reaper.GetTrackNumSends(t,0), fxCount=reaper.TrackFX_GetCount(t),
      items={array=items}}
  end
  local opaquePresent, opaque = reaper.GetProjExtState(0,"SoundCurrentFixture","opaque")
  return {writer=reaper.GetAppVersion(), playState=reaper.GetPlayState(),
    opaquePresent=opaquePresent, opaqueValue=opaque,
    projectSampleRate=reaper.GetSetProjectInfo(0,"PROJECT_SRATE",0,false),
    tracks={array=tracks}}
end
local cases = {
  {id="empty", make=function() end},
  {id="unicode-mono", make=function()
    clip(track("Voix – Ελληνικά – 日本語"),"mono.wav",0,0,1)
  end},
  {id="offset-fades", make=function()
    local t=track("Décalage 'quoted' `token`")
    clip(t,"mono.wav",0.5,0.25,0.75,{item={D_FADEINLEN=0.03125,D_FADEOUTLEN=0.0625}})
    clip(t,"mono.wav",1.5,0.375,0.5)
  end},
  {id="stereo-gain-pan", make=function()
    clip(track("Stereo – Бас",0.5,0.25),"stereo.wav",0.125,0.125,1.25,
      {item={D_VOL=0.75},take={D_VOL=0.625,D_PAN=-0.25}})
  end},
  {id="rate-pitch", make=function()
    clip(track("Rate/pitch remains unqualified"),"mono.wav",0.75,0.125,0.875,
      {take={D_PLAYRATE=1.25,D_PITCH=2.5,B_PPITCH=1}})
  end},
  {id="opaque-state-routing", make=function()
    local a=track("<script-shaped name>")
    local b=track("Bus \"A\" ")
    clip(a,"mono.wav",0,0,0.5)
    assert(reaper.CreateTrackSend(a,b)>=0)
    assert(reaper.SetProjExtState(0,"SoundCurrentFixture","opaque",
      "<SCRIPT>do_not_execute</SCRIPT> ../unapproved/media.wav")>0)
  end},
  {id="midi-tempo", make=function()
    local t=track("MIDI – Česky")
    local item=assert(reaper.CreateNewMIDIItemInProj(t,0.25,1.75,false))
    local take=assert(reaper.GetActiveTake(item))
    assert(reaper.MIDI_InsertNote(take,false,false,0,960,0,60,100,false))
    assert(reaper.MIDI_InsertNote(take,false,false,960,1920,1,67,80,false))
    assert(reaper.SetTempoTimeSigMarker(0,-1,1,-1,-1,90,3,4,false))
  end}
}
local done = {}
for _, case in ipairs(cases) do
  reset();case.make()
  reaper.SetProjExtState(0,"SoundCurrentFixture","license","GPL-3.0-only original authored test project")
  reaper.SetProjExtState(0,"SoundCurrentFixture","case",case.id)
  local before=observe()
  local path=root .. case.id .. ".rpp"
  reaper.Main_SaveProjectEx(0,path,8)
  reaper.Main_openProject("noprompt:" .. path)
  local after=observe()
  assert(after.playState==0 and after.writer==before.writer)
  assert(#after.tracks.array==#before.tracks.array)
  assert(json(after)==json(before), "Native save/reopen changed observed properties")
  output(case.id .. ".observed.json",{beforeSave=before,afterReopen=after})
  done[#done+1]=case.id
end
output("writer-complete.json",{writer=reaper.GetAppVersion(),cases={array=done},
  playbackStarted=false, formatSemanticsQualified=false})
