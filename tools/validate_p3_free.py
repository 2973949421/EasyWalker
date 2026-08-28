"""Read free-session checkpoints; no SD writes and no inferred visual PASS."""
import argparse
import re
import zlib
from pathlib import Path

VERSION='0.7.6-p3c.navfix'

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
    if int(record.get('nav_errors',0)):problems.append('navigation_failure')
    if record.get('display_self_failure','none')!='none':problems.append('display_selfcheck')
    level=int(record.get('volume',-1));raw=int(record.get('speaker_volume_raw',-1))
    if not 0<=level<=255 or int(record.get('speaker_volume_cap',-1))!=102 or raw!=(level*102+127)//255:
        problems.append('volume_policy')
    for key,limit in [('audio_errors',0),('backpressure',0),('pcm_gap_max_us',70000),('present_max_us',100000),('lyric_late_max_ms',200)]:
        if int(record[key])>limit:problems.append(key)
    if problems:return 'FAIL',sorted(set(problems))
    missing=[key for key in ('a_auto','b_auto','c_auto') if record.get(key)!='COVERED']
    if missing:return 'INCOMPLETE',missing
    for key in ('playlist_frames','library_frames','different_track_selections'):
        if int(record.get(key,0))<=0:return 'INCOMPLETE',[key]
    if int(record.get('time_font_px',0))!=14:return 'FAIL',['time_font_px']
    # Recheck measured evidence, rather than trusting a result label.
    if int(record['longest_playing_ms'])<60000:return 'INCOMPLETE',['continuous_playback']
    for key in ('lyrics_frames','cover_frames','lyric_deadline_updates','view_events','volume_events','play_events','library_text_ok'):
        if int(record[key])<=0:return 'INCOMPLETE',[key]
    return 'READY_FOR_REVIEW',['human readability / orientation / listening confirmation still required']

def current_boots(records):
    groups={}
    for record in records:
        if record.get('version')==VERSION:groups.setdefault(int(record['boot_id']),[]).append(record)
    if not groups:raise ValueError('No checkpoints from current firmware; history is preserved, not accepted as new evidence')
    return groups

def reboot_evidence(groups):
    ids=sorted(groups)
    for before,after in zip(ids,ids[1:]):
        saved=[r for r in groups[before] if r.get('manual_checkpoint')=='1']
        if not saved:continue
        a=saved[-1];b=groups[after][-1]
        if (a.get('track')==b.get('restored_track') and a.get('preferred_view')==b.get('restored_view')
            and b.get('startup_paused')=='1' and b.get('startup_silent')=='1' and int(b.get('startup_observed_ms',0))>=3000
            and abs(int(a['position_ms'])-int(b['restored_position_ms']))<=10000):return True
    return False

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log',type=Path)
    args=parser.parse_args()
    records=checkpoints(args.log.read_bytes())
    # No later checkpoint may hide a recorded error from this session.
    groups=current_boots(records)
    current=[r for group in groups.values() for r in group]
    failed=[r for r in current if evaluate(r)[0]=='FAIL']
    record=failed[0] if failed else max(current,key=lambda r:sum(r.get(k)=='COVERED' for k in ('a_auto','b_auto','c_auto')))
    status,details=evaluate(record)
    if status!='FAIL':
        extra=[]
        if not reboot_evidence(groups):extra.append('manual_reboot_not_verified')
        if not any(int(r.get('no_lyrics_view_noop',0)) for r in current):extra.append('no_lyrics_View_not_observed')
        if not any(int(r.get('preference_track_transitions',0)) for r in current):extra.append('cross_track_preference_not_observed')
        if extra:status='INCOMPLETE';details+=extra
    print('boots='+','.join(map(str,groups)))
    print(f'{status}: {", ".join(details)}\ncheckpoint={record["sequence"]} version={record["version"]}')
    print(f'coverage_scope={record.get("coverage_scope","NA")}\nnot_exercised={record.get("not_exercised","NA")}')
    for key in ('track','player_state','longest_playing_ms','pcm_gap_max_us','lyrics_frames','cover_frames','lyric_deadline_updates','resource_path'):
        print(f'{key}={record.get(key,"NA")}')
    raise SystemExit(1 if status=='FAIL' else 0)
