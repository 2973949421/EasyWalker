"""Explicit, private Ave Mujica import. Source audio/LRC are never changed.

Mapping is a preparation recipe, NOT an on-device database. Run --extract
first to inspect embedded jackets. --build requires approved Octagram art.
"""
from pathlib import Path
import argparse, json, shutil, subprocess
from PIL import Image, ImageDraw
from prepare_p3_media import LOCAL, PACKAGE, parse_lrc, pair_cues, cover_ascii, validate_cover, sha

FFDIR=Path('B:/Tools/FFmpeg/ffmpeg-9.0.1-essentials_build/bin')
SOURCE=Path('B:/sharewithlight/SONG')
MAPPING=[
 ('Ave Mujica - ANKOKUTENGOKU (Cover).m4a','ankokutengoku',None),
 ('Ave Mujica - Black Birthday.m4a','blackbirthday','blackbirthday.lrc'),
 ('Ave Mujica - Choir ‘S’ Choir.m4a','choirschoir','choirschoir.lrc'),
 ('Ave Mujica - Ether.m4a','ether','ether.lrc'),
 ('Ave Mujica - Mas uerade Rhapsody Re uest.m4a','masuerade','masuerade.lrc'),
 ('Ave Mujica - Sophie.m4a','sophie','sophie.lrc'),
 ('Ave Mujica - Symbol I   △.m4a','symbol1','symbol1.lrc'),
 ('Ave Mujica - Symbol III   ▽.m4a','symbol3','symbo3.lrc'),
 ('Ave Mujica - Two Moons.m4a','twomoons','twomoons.lrc'),
 ('octagramdance.m4a','octagramdance','octagramdance.lrc'),
]

def run(args):
    return subprocess.run([str(FFDIR/'ffmpeg.exe'),'-hide_banner','-loglevel','error',*args],check=True)
def probe(path):
    p=subprocess.run([str(FFDIR/'ffprobe.exe'),'-v','error','-show_format','-show_streams','-of','json',str(path)],capture_output=True,check=True)
    return json.loads(p.stdout)

def extract():
    jackets=LOCAL/'song-jackets';jackets.mkdir(parents=True,exist_ok=True)
    sheet=Image.new('RGB',(750,900),'white');draw=ImageDraw.Draw(sheet)
    report=[]
    for index,(source,base,lyric) in enumerate(MAPPING):
        info=probe(SOURCE/source);streams=info['streams'];attached=[s for s in streams if s.get('disposition',{}).get('attached_pic')]
        if attached:
            output=jackets/(base+'.jpg')
            run(['-y','-i',str(SOURCE/source),'-map',f'0:{attached[0]["index"]}','-c:v','copy','-frames:v','1',str(output)])
            with Image.open(output) as im:
                assert im.size==(1200,1200), (source,im.size)
                sheet.paste(im.resize((235,235)),((index%3)*250,(index//3)*300))
            draw.text(((index%3)*250,(index//3)*300+240),base,fill='black')
        report.append({'source':source,'basename':base,'lyrics':lyric,'tags':info['format'].get('tags',{}),'embedded_cover':bool(attached),'duration':info['format']['duration']})
    sheet.save(jackets/'contact-sheet.jpg')
    (LOCAL/'SONG_MAPPING.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('EXTRACTED',jackets,flush=True)

def build():
    report=['# 私有媒体交付与逐句译文对照','','原音频、原文及时间戳保持不动；译文为本项目审阅稿。',
            '暗黑天国刻意没有歌词；纯自造咒语不填写译文，不重复占据双语栏。',
            '疑义：Choir 的 S カレート按 escalate 双关译“不断升级”；路加/塔罗语汇仍需结合正式文本审阅。',
            'Sophie 的 Hierophant 暂按塔罗“教皇”；material 保留“物质”直义，不扩写人物隐喻。',
            'Octagram 的 cracker、snuff、puff 为多义/押韵词，分别暂译爆竹、熄灭、吐气；含混语法未强行补造剧情。',
            'Symbol III 的“性”保留原文开放含义；Symbol I 的疑似转录“何がほざく”不修改原稿。',
            '九张封面来自各自用户文件的内嵌图片；Octagram：Completeness 通常盤 BRMM-10917，https://bang-dream.com/discographies/4025/。','']
    for source,base,lyric in MAPPING:
        audio=PACKAGE/'Music/AveMujica'/f'{base}.mp3';audio.parent.mkdir(parents=True,exist_ok=True)
        args=['-y','-i',str(SOURCE/source),'-map','0:a:0','-map_metadata','0','-vn','-ar','44100','-ac','2','-codec:a','libmp3lame','-b:a','320k','-id3v2_version','3']
        if base=='octagramdance':args+=['-metadata','title=Octagram Dance','-metadata','artist=Ave Mujica','-metadata','album=Completeness']
        if not audio.exists():run(args+[str(audio)])
        original=probe(SOURCE/source);converted=probe(audio);stream=converted['streams'][0]
        assert stream['sample_rate']=='44100' and stream['channels']==2 and stream['bit_rate']=='320000'
        assert abs(float(original['format']['duration'])-float(converted['format']['duration']))<.2
        for key in ['title','artist','album']:
            value=original['format'].get('tags',{}).get(key)
            if value:assert converted['format']['tags'][key]==value
        report += [f'## {base}',f'- 输入：{source}',f'- 输出：/Music/AveMujica/{base}.mp3',f'- 字节：{audio.stat().st_size}；SHA-256：{sha(audio)}','']
        if lyric:
            ldir=PACKAGE/'Lyrics/AveMujica';ldir.mkdir(parents=True,exist_ok=True)
            a=SOURCE/lyric;b=LOCAL/'translations'/f'{base}.zh-Hans.lrc'
            shutil.copyfile(a,ldir/f'{base}.lrc');shutil.copyfile(b,ldir/f'{base}.zh-Hans.lrc')
            cues,zh=parse_lrc(a.read_bytes()),parse_lrc(b.read_bytes())
            assert all(ms in {x[0] for x in cues} for ms,_ in zh),'translation timestamp mismatch'
            report += ['| 时间/ms | 用户原文 | 中文审阅稿 |','|---|---|---|']
            report += [f'| {ms} | {jp.replace("|","／")} | {tr.replace("|","／")} |' for ms,jp,tr in pair_cues(cues,zh)]
        else:
            assert not list((PACKAGE/'Lyrics/AveMujica').glob(f'{base}*.lrc'))
        art=LOCAL/'song-jackets'/f'{base}.jpg'
        destination=PACKAGE/'CoverSource/AveMujica'/f'{base}.jpg';destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(art,destination)
        previews=LOCAL/'previews'/base;previews.mkdir(parents=True,exist_ok=True)
        for cols,rows in [(34,26),(40,32),(48,40)]:
            image,data,chars=cover_ascii(art,cols,rows);validate_cover(data)
            image.save(previews/f'{cols}x{rows}.png');image.resize((480,576),Image.Resampling.NEAREST).save(previews/f'{cols}x{rows}-4x.png')
            (previews/f'{cols}x{rows}.txt').write_text(chars,encoding='ascii')
            if(cols,rows)==(40,32):
                dst=PACKAGE/'ADVWalkman/covers/AveMujica'/f'{base}.cover.adv';dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(data)
        print('PREPARED',base,flush=True)
    (LOCAL/'SONG_REVIEW.md').write_text('\n'.join(report)+'\n',encoding='utf-8')
    print('SONG_BATCH_PASS',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--extract',action='store_true');p.add_argument('--build',action='store_true');a=p.parse_args()
    if a.extract:extract()
    if a.build:build()
