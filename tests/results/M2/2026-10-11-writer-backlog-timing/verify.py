# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import gzip,hashlib,json,re
root=Path(__file__).resolve().parent
members=json.loads((root/'manifest.json').read_text())['members']
assert set(members)|{'manifest.json'}=={p.name for p in root.iterdir() if p.is_file()}
for name,m in members.items():
 raw=(root/name).read_bytes();assert len(raw)==m['bytes'] and hashlib.sha256(raw).hexdigest()==m['sha256']
 if m.get('lossless'):
  raw=gzip.decompress(raw);assert len(raw)==m['originalBytes'] and hashlib.sha256(raw).hexdigest()==m['originalSha256']
summary=json.loads((root/'summary.json').read_text());qualified=json.loads((root/'qualified-profiles.json').read_text());frozen=json.loads((root/'frozen-inputs.json').read_text())
assert summary['source']==qualified['source']==frozen['source']=='3ed02b44b206250a2726cefb59d2190c1e33cc9e'
assert summary['tree']==qualified['tree']==frozen['tree'] and summary['selectedInputs']==len(frozen['selectedInputs'])==636
assert summary['changedSourceFiles']==['tests/manual_recording_tests.cpp'] and not summary['nativeWindowsRootResolved'] and not summary['productionCapacityOrDeadlinesChanged']
for label in ['release','sanitizer','tsan']:
 test=gzip.decompress((root/(label+'-tests.log.gz')).read_bytes()).decode();log=gzip.decompress((root/(label+'-LastTest.log.gz')).read_bytes()).decode()
 assert '100% tests passed, 0 tests failed out of 1' in test
 actual=json.loads(next(l for l in log.splitlines() if l.startswith('{"checks":')));assert actual==summary['observed'][label]['workflow'] and actual['rt_violations']==0 and actual['maximum_output_difference']==0 and actual['repeated_workflows']==3
 held=re.search(r'held_first_write_diagnostic acquired_frames=256 capacity_frames=8192 held_age_ns=(\d+) completed_fixture_span_max_ns=(\d+) queue_full_prefix=8192 raw_exact=true windows_root_inferred=false',log)
 assert held and int(held[2])>=int(held[1])>=20000000 and int(held[1])==summary['observed'][label]['controlledHoldAgeNs'] and int(held[2])==summary['observed'][label]['controlledMaximumWriteSpanNs']
 if label!='release':assert len(qualified['profiles'][label]['productionArchivesReadOnly'])==19
assert summary['windows']['warningFree'] and not summary['windows']['runtimeExecuted'] and not gzip.decompress((root/'windows-manual-fixture-compile.log.gz').read_bytes())
protocol=gzip.decompress((root/'release-standalone-protocol.log.gz').read_bytes()).decode();assert 'concurrent_writer_timing' in protocol and 'original_concurrent_owner_diagnostic completed20takes; original5sdeadline; native_audio=false' in protocol
print('Verified original held-write/raw-prefix evidence, three Linux profiles, unchanged production limits and Windows compile-only scope')
