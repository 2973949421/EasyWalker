"""Read free-session checkpoints; no SD writes and no inferred visual PASS."""
import argparse
import re
import zlib
from pathlib import Path

VERSION='0.10.0-p5.sound'

def evaluate_render(record):
    required=('render_contract','render_pixel_selfcheck','frame_starts','frame_rejects','frame_repairs',
              'frame_failure','frame_pending','frame_expected_rows','frame_submitted_rows','frame_complete_ms','full_frames')
    absent=[key for key in required if key not in record]
    failures=[]
    if int(record.get('frame_rejects',0)) or record.get('frame_failure','none')!='none':failures.append('frame_integrity_failure')
    if record.get('render_pixel_selfcheck','PASS')!='PASS':failures.append('render_pixel_selfcheck')
    if 'frame_expected_rows' in record and int(record.get('frame_submitted_rows',0))>int(record['frame_expected_rows']):failures.append('frame_row_overrun')
    if int(record.get('frame_complete_ms',0)) and (record.get('frame_pending')!='0' or record.get('frame_expected_rows')!=record.get('frame_submitted_rows')):
        failures.append('frame_false_completion')
    if failures:return 'FAIL',failures
    if absent:return 'INCOMPLETE',absent
    if record['render_contract']!='1':return 'FAIL',['render_contract']
    if int(record['frame_starts'])<=0 or int(record['full_frames'])<=0:return 'INCOMPLETE',['no_full_submission']
    return 'READY_FOR_REVIEW',['submission contract covered; screen appearance still requires human review']

def evaluate_wake(record):
    required=('reset_reason','rtc_diagnostic_bytes','wake_complete','wake_unfinished_count',
              'wake_captured_ms','wake_backlight_ms','wake_resume_ms','wake_first_frame_ms','wake_unlock_ms',
              'wake_resume_pcm','pcm_buffers','wake_resume_position_ms','position_ms')
    absent=[k for k in required if k not in record]
    if absent:return 'INCOMPLETE',absent
    if record['rtc_diagnostic_bytes']!='0':return 'FAIL',['rtc_diagnostic_storage']
    if int(record['wake_unfinished_count']):return 'INCOMPLETE',['wake_interrupted_before_first_frame']
    if record['wake_complete']!='YES':return 'INCOMPLETE',['wake_not_completed']
    # No timestamp ordering across uint32 wrap; compare modular deltas.
    start=int(record['wake_captured_ms'])
    if any(((int(record[k])-start)&0xffffffff)>0x7fffffff for k in ('wake_backlight_ms','wake_resume_ms','wake_first_frame_ms','wake_unlock_ms')):
        return 'FAIL',['wake_timestamp_order']
    return 'READY_FOR_REVIEW',['wake observation present; human sound/input confirmation required']

def evaluate_p3d(record):
    failures=[k for k in ('settings_errors','launcher_errors','library_cover_errors') if int(record.get(k,0))]
    if failures:return 'FAIL',failures
    missing=[k for k in ('settings_changes','settings_writes','screen_sleeps','screen_wakes','library_cover_frames','vinyl_frames') if int(record.get(k,0))<=0]
    if record.get('d_auto')!='COVERED':missing.append('d_auto')
    return ('INCOMPLETE',missing) if missing else ('READY_FOR_REVIEW',['human visual and wake-key review required'])

def display_reboot_evidence(groups):
    ids=sorted(groups)
    for before,after in zip(ids,ids[1:]):
        saved=[r for r in groups[before] if r.get('manual_checkpoint')=='1' and r.get('settings_saved')=='1']
        if not saved:continue
        a=saved[-1];b=groups[after][-1]
        if b.get('display_settings_loaded')=='1' and all(a.get(key)==b.get('restored_'+key) for key in ('brightness','player_timeout_ms','other_timeout_ms')):return True
    return False

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

def save_records(data):
    """Return independently committed SAVE_BEGIN/SAVE_END records.

    A torn full snapshot is intentionally ignored by checkpoints(), while a
    later short SAVE_END remains readable and can report partial success.
    """
    records=[]
    for line in data.splitlines(keepends=True):
        match=re.fullmatch(rb'(SAVE_(?:BEGIN|END) .+ )crc=([0-9a-fA-F]{8})\n',line)
        if not match:continue
        if int(match[2],16)!=zlib.crc32(match[1]):raise ValueError('SAVE record CRC mismatch')
        fields=dict(item.split('=',1) for item in match[1].decode('utf-8').strip().split()[1:])
        fields['record']=match[1].split(b' ',1)[0].decode('ascii');records.append(fields)
    return records

def evaluate(record):
    if record.get('version')!=VERSION or record.get('mode')!='free':raise ValueError('Wrong firmware/log format')
    problems=[]
    if record.get('result')=='FAIL':problems.append('device_reported_failure')
    if record.get('failure_reason')!='none':problems.append(record.get('failure_reason','missing_failure_field'))
    if int(record.get('nav_errors',0)):problems.append('navigation_failure')
    if int(record.get('tab_state_errors',0)):problems.append('tab_changed_transport')
    for key in ('cover_failure','lyrics_failure','font_failure','navigation_failure'):
        if record.get(key,'none')!='none':problems.append(key)
    if record.get('display_self_failure','none')!='none':problems.append('display_selfcheck')
    level=int(record.get('volume',-1));raw=int(record.get('speaker_volume_raw',-1))
    if not 0<=level<=255 or int(record.get('speaker_volume_cap',-1))!=102 or raw!=(level*102+127)//255:
        problems.append('volume_policy')
    for key,limit in [('audio_errors',0),('backpressure',0),('pcm_gap_max_us',70000),('present_max_us',100000),('lyric_late_max_ms',200)]:
        if int(record[key])>limit:problems.append(key)
    if problems:return 'FAIL',sorted(set(problems))
    for key,limit in [('input_accept_max_ms',50),('selection_feedback_max_ms',100),('warm_return_max_ms',300),('view_warm_max_ms',300),('view_cold_max_ms',1500),('view_failures',0),('input_queue_overflow',0)]:
        if key not in record:return 'INCOMPLETE',[key]
        if int(record[key])>limit:return 'FAIL',[key]
    render_status,render_details=evaluate_render(record)
    if render_status!='READY_FOR_REVIEW':return render_status,render_details
    missing=[key for key in ('a_auto','b_auto','c_auto') if record.get(key)!='COVERED']
    if missing:return 'INCOMPLETE',missing
    for key in ('playlist_frames','library_frames','different_track_selections','tab_playing','tab_paused','warm_returns','view_warm_completed','view_cold_completed'):
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

def full_records(records):
    """Periodic summaries preserve failures, but never stand in for acceptance evidence."""
    return [record for record in records if record.get('snapshot','full')=='full']

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
    data=args.log.read_bytes();records=checkpoints(data);saves=save_records(data)
    begun={(item.get('boot_id','0'),item['ticket']) for item in saves if item['record']=='SAVE_BEGIN'}
    ended={(item.get('boot_id','0'),item['ticket']) for item in saves if item['record']=='SAVE_END'}
    if begun-ended:raise ValueError('T save has no terminal record: '+','.join('/'.join(value) for value in sorted(begun-ended)))
    # No later checkpoint may hide a recorded error from this session.
    groups=current_boots(records)
    current=[r for group in groups.values() for r in group]
    summary_failures=[r for r in current if r.get('snapshot')=='summary' and (r.get('result')=='FAIL' or r.get('failure_reason','none')!='none')]
    detailed=full_records(current)
    if not detailed:raise ValueError('No full checkpoint; periodic summary is not acceptance evidence')
    failed=[r for r in detailed if evaluate(r)[0]=='FAIL' or evaluate_p3d(r)[0]=='FAIL' or evaluate_wake(r)[0]=='FAIL']
    record=failed[0] if failed else max(detailed,key=lambda r:(sum(r.get(k)=='COVERED' for k in ('a_auto','b_auto','c_auto','d_auto')),int(r['boot_id']),int(r['sequence'])))
    status,details=evaluate(record)
    if summary_failures:
        status='FAIL';details+=['summary_recorded_failure:'+summary_failures[0].get('failure_reason','unknown')]
    d_status,d_details=evaluate_p3d(record)
    if d_status=='FAIL':status='FAIL';details+=d_details
    elif status=='READY_FOR_REVIEW' and d_status=='INCOMPLETE':status='INCOMPLETE';details+=d_details
    if status!='FAIL':
        extra=[]
        wake_status,wake_details=evaluate_wake(record)
        if wake_status=='FAIL':status='FAIL';details+=wake_details
        elif wake_status=='INCOMPLETE':extra+=wake_details
        if not reboot_evidence(groups):extra.append('manual_reboot_not_verified')
        if not display_reboot_evidence(groups):extra.append('display_settings_reboot_not_verified')
        if not any(int(r.get('no_lyrics_view_noop',0)) for r in current):extra.append('no_lyrics_View_not_observed')
        if not any(int(r.get('preference_track_transitions',0)) for r in current):extra.append('cross_track_preference_not_observed')
        for scene in ('lyrics','cover','playlist'):
            if max(int(r.get('wake_'+scene,0)) for r in current)<2:extra.append('two_wakes_'+scene+'_not_observed')
        if extra:
            if status!='FAIL':status='INCOMPLETE'
            details+=extra
    print('boots='+','.join(map(str,groups)))
    print(f'{status}: {", ".join(details)}\ncheckpoint={record["sequence"]} version={record["version"]}')
    print(f'coverage_scope={record.get("coverage_scope","NA")}\nnot_exercised={record.get("not_exercised","NA")}')
    for key in ('track','player_state','longest_playing_ms','pcm_gap_max_us','lyrics_frames','cover_frames','lyric_deadline_updates','resource_path'):
        print(f'{key}={record.get(key,"NA")}')
    raise SystemExit(1 if status=='FAIL' else 0)
