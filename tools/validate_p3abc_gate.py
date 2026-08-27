"""Read-only combined P3A-fix/B/C device-log and Session validator."""
import argparse
import re
from pathlib import Path
from inspect_player_state import inspect_file

VERSION='0.7.0-p3c.media'

def fields(text):
    result={}
    for line in text.splitlines():
        for key,value in re.findall(r'(\w+)=([^\s]+)',line):result[key]=value
    return result

def require(condition,reason):
    if not condition:raise ValueError(reason)

def validate(logs):
    a,b,c=logs
    for label,log in zip('ABC',logs):require(log.get('version')==VERSION,f'{label}: stale version')
    require(a.get('result')=='PASS','A did not pass')
    require(int(a.get('library_text_lines',0))==2,'A: not two lines')
    require(int(a.get('library_text_width_px',999))<=int(a.get('library_text_available_px',0)),'A: overflow')
    for key in ('library_text_truncated','library_text_invalid_utf8','library_text_layout_error'):
        require(a.get(key)=='0',f'A: {key}')
    require(a.get('library_text_is_benchmark')=='1','A: wrong tested library')
    require(a.get('orientation')=='135x240' and a.get('rotation')=='2','A: orientation')
    require(a.get('sample_rate')=='44100' and a.get('player_state')=='PLAYING','A: playback')
    for key,expected in (('backpressure','0'),('pcm_gap_over_100ms','0'),('player_error','NONE'),('audio_error','none')):
        require(a.get(key)==expected,f'A: {key}')
    require(int(a.get('pcm_buffers',0))>0,'A: no PCM')
    require(b.get('result')=='PASS' and b.get('task_executed')=='1','B did not run/pass')
    require(int(b.get('measurement_ms',0))>=10000,'B measurement too short')
    for key in ('model_failure','draw_failure','overlay_failure','audio_failure'):
        require(b.get(key)=='none',f'B: {key}')
    require(c.get('result')=='RUNNING' and c.get('final_result')=='PASS','C: missing pre/post-reboot evidence')
    for key in ('task_executed','display_confirmed','lyrics_confirmed','cover_confirmed','view_key_confirmed',
                'pause_checked','seek_checked','fallback_checked','audio_user_confirmed','reboot_checked','restore_paused'):
        require(c.get(key)=='1',f'C missing check: {key}')
    require(c.get('restore_view')=='cover','C: restore view')
    for label,log in [('B',b),('C',c)]:
        require(log.get('sample_rate')=='44100',f'{label}: sample rate')
        require(log.get('backpressure')=='0',f'{label}: backpressure')
        require(log.get('audio_error')=='none',f'{label}: audio error')
        require(log.get('audio_error_events')=='0',f'{label}: error events')
        require(int(log.get('pcm_buffers',0))>10,f'{label}: no PCM')
        require(int(log.get('pcm_gap_max_us',9999999))<=70000,f'{label}: PCM gap')
    for key in ('font_missing','font_io_errors','font_draw_misses','lyrics_layout_error','lyrics_invalid_utf8'):
        require(c.get(key)=='0',f'C: {key}')
    require(int(c.get('measurement_frames',0))>=6,'C: media frames not exercised')
    require(int(c.get('measurement_resource_bytes',0))>0,'C: cold reads not exercised')
    require(0<int(c.get('media_budget_bytes',0))<=48*1024,'C: memory budget')
    return True

def main(root):
    path=root/'ADVWalkman/logs'
    logs=[fields((path/name).read_text(encoding='utf-8-sig')) for name in ('p3a-last.txt','p3b-last.txt','p3c-last.txt')]
    validate(logs)
    sessions=[]
    for suffix in ('a','b'):
        file=root/f'ADVWalkman/state/session-{suffix}.bin'
        if file.exists():
            try:sessions.append(inspect_file(file))
            except ValueError:pass
    require(sessions,'No valid Session CRC')
    latest=max(sessions,key=lambda s:s['generation'])
    require(latest['preferred_now_playing_view']=='cover','Latest Session does not store tested view')
    print('P3ABC LOGS + SESSION CRC: PASS')
    print('Physical readability/direction/cover/audio confirmations are recorded in the log; no build-only PASS.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--sd-root',type=Path,required=True)
    main(parser.parse_args().sd_root.resolve(strict=True))
