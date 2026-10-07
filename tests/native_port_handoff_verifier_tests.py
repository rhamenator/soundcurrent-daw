#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutate a retained handoff trace; no audio run or fabricated native baseline."""
import argparse
import copy
import json
from pathlib import Path
from verify_native_port_handoff import analyze


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    h = json.loads((args.project / 'native-port-handoff.json').read_text())
    m = {role: json.loads((args.project / (role + '-port-markers.json')).read_text())
         for role in ['owner', 'source']}
    analyze(h, m)
    owner = next(n for n, f in enumerate(h['filters']) if 'recording-manual' in f['name'])
    prefix = ['filters', owner]
    cases = [(key, prefix + [key], 1) for key in ['dropped', 'query_overflows', 'unknown_queries',
             'unknown_events', 'outer_allocations', 'outer_frees', 'outer_locks']]
    cases += [('port layout', prefix + ['ports', 0, 'input'], False),
              ('callback count', prefix + ['calls'], 1),
              ('clock correspondence', prefix + ['rows', 0, 'cycle'], 0),
              ('unknown clock', prefix + ['rows', 0, 'clock_known'], False),
              ('buffer coverage', prefix + ['rows', 0, 'queries', 0, 'known_buffer'], False),
              ('buffer extent', prefix + ['rows', 0, 'queries', 0, 'maximum_bytes'], 1),
              ('query layout', prefix + ['rows', 0, 'queries', 0, 'port'], 2),
              ('buffer presence', prefix + ['rows', 0, 'queries', 0, 'returned'], False)]
    cases += [('undeclared API suppression', prefix + ['rows', 0, 'queries', 0, 'api_suppressed'], True)]
    cases += [('timing ' + key, prefix + ['wrapper_timing', key], False)
              for key in ['complete_timing_coverage', 'complete_cpu_coverage',
                          'complete_thread_usage_coverage', 'finite_deadline_thresholds_met']]
    refused = []
    for name, path, value in cases:
        altered = copy.deepcopy(h)
        parent = altered
        for key in path[:-1]: parent = parent[key]
        parent[path[-1]] = value
        try: analyze(altered, m)
        except (AssertionError, KeyError, ValueError): refused.append(name)
        else: raise AssertionError('Mutation accepted: ' + name)
    marker_cases = [('declared channels ' + str(channels), ['owner', 'channels'], channels)
                    for channels in [0, 31, 33]]
    marker_cases += [('wrong marker role', ['owner', 'source'], True),
                     ('wrong marker query count', ['owner', 'expected_buffer_calls'], 32),
                     ('missing channel ordinal', ['owner', 'rows', 0, 'ports', 0, 'channel'], 4),
                     ('unseen channel', ['owner', 'rows', 0, 'ports', 0, 'seen'], False),
                     ('wrong channel extent', ['owner', 'rows', 0, 'ports', 0, 'frames'], 1)]
    for name, path, value in marker_cases:
        altered = copy.deepcopy(m)
        parent = altered
        for key in path[:-1]: parent = parent[key]
        parent[path[-1]] = value
        try: analyze(h, altered)
        except (AssertionError, KeyError, ValueError): refused.append(name)
        else: raise AssertionError('Marker mutation accepted: ' + name)
    for role in ['owner', 'source']:
        for alteration in ['missing marker', 'empty marker', 'null marker', 'missing row', 'extra row', 'missing port']:
            altered = copy.deepcopy(m)
            if alteration == 'missing marker': del altered[role]
            elif alteration == 'empty marker': altered[role] = {}
            elif alteration == 'null marker': altered[role] = None
            elif alteration == 'missing row': altered[role]['rows'].pop()
            elif alteration == 'extra row': altered[role]['rows'].append(copy.deepcopy(altered[role]['rows'][0]))
            else: altered[role]['rows'][0]['ports'].pop()
            try: analyze(h, altered)
            except (AssertionError, KeyError, ValueError): refused.append(role + ' ' + alteration)
            else: raise AssertionError('Missing coverage accepted: ' + role + ' ' + alteration)
    args.output.write_text(json.dumps({'retained_native_baseline': str(args.project),
                                      'mutations_refused': refused}, indent=2) + '\n')
    print(json.dumps({'mutations_refused': len(refused)}))


if __name__ == '__main__':
    main()
