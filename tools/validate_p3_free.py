"""Read free-session checkpoints; no SD writes and no inferred visual PASS."""
import argparse
import re
import zlib
from pathlib import Path

VERSION='0.7.4-p3c.tune'

def checkpoints(data):
    result=[]
    current=None
    sequence=None
    for line in data.splitlines(keepends=True):
        if line.startswith(b'BEGIN sequence='):
            current=bytearray(line)
            sequence=int(line.split(b'=')[1])
        elif current is not None and line.startswith(b'END '):
            end=re.fullmatch(rb'END sequence=(\d+) crc=([0-9a-fA-F]{8})\n',line)
            if not end:raise ValueError('Malformed checkpoint commit line')
            if int(end[1])!=sequence or int(end[2],16)!=zlib.crc32(current):raise ValueError('Checkpoint CRC/sequence mismatch')
            fields=dict(s.split('=',1) for s in current.decode('utf-8').splitlines()[1:] if '=' in s)
            fields['sequence']=str(sequence)
            result.append(fields);current=None
        elif current is not None:
            current.extend(line)
    if not result:raise ValueError('No complete checkpoint; do not report PASS')
    return result  # A trailing incomplete checkpoint never erases earlier evidence.

def evaluate(record):
    if record.get('version')!=VERSION or record.get('mode')!='free':raise ValueError('Wrong firmware/log format')
    problems=[]
    if record.get('result')=='FAIL':problems.append('device_reported_failure')
    if record.get('failure_reason')!='none':problems.append(record.get('failure_reason','missing_failure_field'))
    level=int(record.get('volume',-1));raw=int(record.get('speaker_volume_raw',-1))
    if not 0<=level<=255 or int(record.get('speaker_volume_cap',-1))!=63 or raw!=(level*63+127)//255:
        problems.append('volume_policy')
    for key,limit in [('audio_errors',0),('backpressure',0),('pcm_gap_max_us',70000),('present_max_us',100000),('lyric_late_max_ms',200)]:
        if int(record[key])>limit:problems.append(key)
    if problems:return 'FAIL',sorted(set(problems))
    missing=[key for key in ('a_auto','b_auto','c_auto') if record.get(key)!='COVERED']
    if missing:return 'INCOMPLETE',missing
    # Recheck measured evidence, rather than trusting a result label.
    if int(record['longest_playing_ms'])<60000:return 'INCOMPLETE',['continuous_playback']
    for key in ('lyrics_frames','cover_frames','lyric_deadline_updates','view_events','volume_events','play_events','library_text_ok'):
        if int(record[key])<=0:return 'INCOMPLETE',[key]
    return 'READY_FOR_REVIEW',['human readability / orientation / listening confirmation still required']

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log',type=Path)
    args=parser.parse_args()
    records=checkpoints(args.log.read_bytes())
    # No later checkpoint may hide a recorded error from this session.
    failed=[r for r in records if evaluate(r)[0]=='FAIL']
    record=failed[0] if failed else records[-1]
    status,details=evaluate(record)
    print(f'{status}: {", ".join(details)}\ncheckpoint={record["sequence"]} version={record["version"]}')
    print(f'coverage_scope={record.get("coverage_scope","NA")}\nnot_exercised={record.get("not_exercised","NA")}')
    for key in ('track','player_state','longest_playing_ms','pcm_gap_max_us','lyrics_frames','cover_frames','lyric_deadline_updates','resource_path'):
        print(f'{key}={record.get(key,"NA")}')
    raise SystemExit(1 if status=='FAIL' else 0)
