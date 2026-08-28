"""Local-only 0.8.2 resource preparation; never writes SD or edits original LRC.

Translation prose is private, supplied via an ignored review recipe. This tool
preserves a baseline, checks timestamps, emits the old/new review, and remuxes
the authorized benchmark audio without transcoding. Delivery is separate.
"""
import argparse,importlib.util,json,re,shutil,subprocess
from pathlib import Path
from prepare_p3_media import LOCAL,PACKAGE,parse_lrc,pair_cues,sha
from prepare_song_library import FFDIR,probe

def audio_hash(path):
    return subprocess.check_output([str(FFDIR/'ffmpeg.exe'),'-v','error','-i',str(path),'-map','0:a:0','-c','copy','-f','hash','-hash','sha256','-']).decode().strip()

def prepare(source):
    spec=importlib.util.spec_from_file_location('private_revision',LOCAL/'translations/revision_082.py')
    revision=importlib.util.module_from_spec(spec);spec.loader.exec_module(revision)
    baseline=LOCAL/'perf-baseline';baseline.mkdir(exist_ok=True)
    notes=['# 0.8.2 逐句译文复核','','原文与时间戳不改；旧译单独留存。咒语不强加中文。以下译文仍为可修订的审阅稿。',
           '', '## 有意保留的疑义',
           '- Crucifix X：用户原稿「戦標」「練外」与授权歌词页「戦慄」「疎外」不同；仅在译意中参考后者，原稿不改。https://www.uta-net.com/song/372661/',
           '- Crucifix X 的 Logos 暂按语境译“理性”，Fortuna 为命运女神；不据此补写剧情。',
           '- Sophie 的 material 暂保留“物质”，不擅自指认为某个角色或肉身。',
           '- Octagram Dance 有意打散语法并密集押韵；cracker / snuff / puff 存在多义，暂取爆竹 / 熄灭 / 吐气。不能把这些暂译当唯一解释。',
           '- Symbol III 的“性”按上下文暂译“本性”；保留讨论空间。Symbol I 的“何がほざく”保留用户写法。',
           '', '## 原文／旧译／新译','']
    total=0;report=[]
    for base,changes in revision.CHANGES.items():
        fixture=PACKAGE/'Lyrics/ADVWalkmanBenchmark/benchmark.lrc' if base=='crucifix-x' else PACKAGE/'Lyrics/AveMujica'/f'{base}.lrc'
        old=fixture.with_name(fixture.stem+'.zh-Hans.lrc')
        saved=baseline/'translations'/f'{base}.zh-Hans.lrc';saved.parent.mkdir(exist_ok=True)
        if not saved.exists():shutil.copy2(old,saved)
        original_bytes=fixture.read_bytes();original=parse_lrc(original_bytes)
        previous=parse_lrc(saved.read_bytes());new=dict(previous);delta=parse_lrc(changes.strip().encode())
        stamps={ms for ms,_ in original}
        assert all(ms in stamps for ms,_ in delta),(base,'new timestamp not in original')
        assert all(ms in new for ms,_ in delta),(base,'change unexpectedly adds a translation')
        new.update(delta)
        target=PACKAGE/'Lyrics/AveMujica'/f'{base}.lrc';target.parent.mkdir(exist_ok=True)
        if target!=fixture:shutil.copy2(fixture,target)
        lines=[]
        for ms,text in sorted(new.items()):
            lines.append(f'[{ms//60000:02}:{ms//1000%60:02}.{ms%1000:03}]{text}')
        translated=target.with_name(base+'.zh-Hans.lrc')
        translated.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        shutil.copy2(translated,LOCAL/'translations'/translated.name)
        assert target.read_bytes()==original_bytes
        assert [ms for ms,_ in parse_lrc(translated.read_bytes())]==[ms for ms,_ in previous]
        notes += [f'### {base}','','| 时间 | 用户原文 | 旧译 | 新译 |','|---|---|---|---|']
        before=dict(previous)
        for ms,ja in original:
            escape=lambda s:s.replace('|','／').replace('\n',' ')
            notes.append(f'| {ms//60000:02}:{ms//1000%60:02}.{ms%1000:03} | {escape(ja)} | {escape(before.get(ms,"（不译）"))} | {escape(new.get(ms,"（不译）"))} |')
        notes.append('');total+=len(original)
        report.append(dict(song=base,cues=len(original),changed=sum(before[t]!=v for t,v in new.items()),original_sha256=sha(target),translation_sha256=sha(translated)))
    assert len(report)==10 and total==298,(len(report),total)
    (LOCAL/'TRANSLATION_REVIEW_082.md').write_text('\n'.join(notes)+'\n',encoding='utf-8')
    backup=baseline/'benchmark.mp3'
    if not backup.exists():shutil.copy2(source,backup)
    destination=PACKAGE/'Music/AveMujica/crucifix-x.mp3'
    subprocess.run([str(FFDIR/'ffmpeg.exe'),'-v','error','-y','-i',str(backup),'-map','0:a:0','-map_metadata','0','-c:a','copy','-metadata','title=Crucifix X','-metadata','artist=Ave Mujica','-id3v2_version','3',str(destination)],check=True)
    old_probe,new_probe=probe(backup),probe(destination)
    old_hash,new_hash=audio_hash(backup),audio_hash(destination)
    assert old_hash==new_hash,'Audio packet content changed'
    assert abs(float(old_probe['format']['duration'])-float(new_probe['format']['duration']))<.03
    for key in ('album','date','genre','track'):
        value=old_probe['format'].get('tags',{}).get(key)
        if value:assert new_probe['format']['tags'].get(key)==value
    assert new_probe['format']['tags']['title']=='Crucifix X'
    cover=PACKAGE/'ADVWalkman/covers/ADVWalkmanBenchmark/benchmark.cover.adv'
    shutil.copy2(cover,PACKAGE/'ADVWalkman/covers/AveMujica/crucifix-x.cover.adv')
    art=PACKAGE/'CoverSource/ADVWalkmanBenchmark/benchmark.jpg'
    if art.exists():shutil.copy2(art,PACKAGE/'CoverSource/AveMujica/crucifix-x.jpg')
    assert len(list((PACKAGE/'Music/AveMujica').glob('*.mp3')))==11
    assert not list((PACKAGE/'Lyrics/AveMujica').glob('ankokutengoku*.lrc'))
    result=dict(version='0.8.2-p3d.perf',lyrics=report,cues=total,audio_packet_hash=old_hash,audio_duration=new_probe['format']['duration'],audio_sha256=sha(destination),cover_sha256=sha(cover))
    (LOCAL/'perf-resources.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--benchmark-source',type=Path,required=True);a=p.parse_args();prepare(a.benchmark_source)
