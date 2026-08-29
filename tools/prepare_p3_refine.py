"""0.8.3 private, local-only tag/font/preview compiler; no SD writes.

Existing basenames, lyrics and artwork are intentionally untouched. Generated
preview images are PC pixel-layout references, not device screenshots.
"""
import argparse,json,math,shutil,struct,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
from prepare_p3_media import LOCAL,PACKAGE,make_font,font_sources,sha
from prepare_song_library import FFDIR,probe
from prepare_p3_perf import audio_hash

TITLES={
 'ankokutengoku':'暗黒天国','blackbirthday':'黒のバースデイ',
 'choirschoir':'Choir ‘S’ Choir','crucifix-x':'Crucifix X','ether':'Ether',
 'masuerade':'Mas?uerade Rhapsody Re?uest','octagramdance':'八芒星ダンス',
 'sophie':'Sophie','symbol1':'Symbol I : △','symbol3':'Symbol III : ▽',
 'twomoons':'ふたつの月 ~Deep Into The Forest~'}
SOURCES={
 'ankokutengoku':'https://bang-dream.com/discographies/3669/',
 'blackbirthday':'https://bang-dream.com/discographies/3376/',
 'twomoons':'https://bang-dream.com/discographies/3376/',
 'choirschoir':'https://bang-dream.com/discographies/3376/',
 'masuerade':'https://bang-dream.com/discographies/3376/',
 'octagramdance':'https://bang-dream.com/discographies/4025/',
 'crucifix-x':'https://bang-dream.com/discographies/4025/',
 'sophie':'https://bang-dream.com/discographies/4125/',
}
PRIVATE=LOCAL/'refine-083'

def backup(path):
    saved=PRIVATE/'recovery'/path.relative_to(PACKAGE)
    saved.parent.mkdir(parents=True,exist_ok=True)
    if not saved.exists():shutil.copy2(path,saved)
    return saved

def tags():
    report=[]
    for base,title in TITLES.items():
        target=PACKAGE/'Music/AveMujica'/f'{base}.mp3'
        before=probe(target);old=before['format'].get('tags',{})
        saved=PRIVATE/'recovery'/target.relative_to(PACKAGE)
        changed=old.get('title')!=title or saved.exists()
        if old.get('title')!=title:
            if base not in ('ankokutengoku','blackbirthday','twomoons','octagramdance'):
                raise ValueError(f'Unexpected tag correction: {base}: {old}')
            source=backup(target);before=probe(source);old=before['format']['tags']
            temporary=PRIVATE/f'{base}.mp3'
            subprocess.run([str(FFDIR/'ffmpeg.exe'),'-v','error','-y','-i',str(source),
                '-map','0','-map_metadata','0','-c','copy','-metadata',f'title={title}',
                '-id3v2_version','3',str(temporary)],check=True)
            after=probe(temporary)
            assert audio_hash(source)==audio_hash(temporary),(base,'audio packet change')
            assert abs(float(before['format']['duration'])-float(after['format']['duration']))<.03
            for key,value in old.items():
                if key.lower() not in ('title','encoder'):
                    assert after['format']['tags'].get(key)==value,(base,key)
            for a,b in zip(before['streams'],after['streams']):
                for key in ('codec_name','sample_rate','channels'):
                    if key in a:assert a[key]==b[key],(base,key)
            shutil.copy2(temporary,target)
        assert probe(target)['format']['tags']['title']==title
        report.append(dict(file=f'Music/AveMujica/{base}.mp3',title=title,changed=changed,
            sha256=sha(target),audio_packet_hash=audio_hash(target),
            source=SOURCES.get(base,'https://bang-dream.com/discographies/3832/')))
    assert not list((PACKAGE/'Lyrics/AveMujica').glob('ankokutengoku*.lrc'))
    (PRIVATE/'titles.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('11 titles checked; changed:',[r['file'] for r in report if r['changed']],flush=True)

def source(path):
    from fontTools.ttLib import TTFont
    path=Path(path);font=TTFont(path,fontNumber=0,lazy=True)
    points=set(font.getBestCmap());font.close();return path,points

def records(stem):
    raw=stem.with_suffix('.idx').read_bytes()
    return {r[0]:r for r in (struct.unpack_from('<IIHHhhhHI',raw,i) for i in range(16,len(raw),24))}

def fonts():
    destination=PACKAGE/'ADVWalkman/fonts';reports=[]
    # Keep the existing repertoire, including kana/symbols; fallback sources
    # are explicit in the generated report, never silently renamed.
    cjk={cp for cp in records(destination/'cjk-12') if cp>=256}
    cjk.update(map(ord,'未分类暂无曲库华文行楷音乐收藏'))
    primary=source('C:/Windows/Fonts/STXINGKA.TTF')
    script=source('C:/Windows/Fonts/KUNSTLER.TTF')
    for size in (12,18):
        name=f'library-cjk-{size}';print('Generating',name,flush=True)
        reports.append(make_font(name,size,cjk,[primary,*font_sources(False)],destination))
    for size in (14,22):
        reports.append(make_font(f'library-latin-{size}',size,[*range(32,127),*range(160,256)],[script,*font_sources(True)],destination))
    required={ord(c) for t in TITLES.values() for c in t if ord(c)>=256}
    for name in ('cjk-12','cjk-14'):
        known=set(records(destination/name));missing=required-known
        if missing:
            for ext in ('.vlw','.idx','.idx2'):backup((destination/name).with_suffix(ext))
            reports.append(make_font(name,int(name.split('-')[1]),known|required,font_sources(False),destination))
    assert all(not r['missing'] for r in reports),[(r['name'],r['missing']) for r in reports]
    (PRIVATE/'fonts.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Font resources complete',flush=True)

def previews():
    faces={}
    for name in ('library-cjk-12','library-cjk-18','library-latin-14','library-latin-22'):
        stem=PACKAGE/'ADVWalkman/fonts'/name;faces[name]=(records(stem),stem.with_suffix('.vlw').read_bytes())
    def glyph(c,small=False):
        name=f'library-{"latin" if ord(c)<256 else "cjk"}-{(14 if ord(c)<256 else 12) if small else (22 if ord(c)<256 else 18)}'
        rs,bits=faces[name];_,off,w,h,adv,dx,dy,_,_=rs[ord(c)]
        return Image.frombytes('L',(w,h),bits[off:off+w*h]) if w*h else Image.new('L',(1,1)),adv,dx,dy
    def text(im,s,x,y):
        for c in s:
            mask,a,dx,dy=glyph(c);im.paste('white',(x+dx,y+dy),mask);x+=a
    cover=Image.open(LOCAL/'p3d/library-cover-135.png').convert('RGB')
    for name,count,index in [('AveMujica',3,1),('未分类',1,0),('华文行楷音乐收藏',2,0),('AveMujica',0,0)]:
        lines=[''];width=0
        for c in name:
            a=glyph(c)[1]
            if width+a>123:lines.append('');width=0
            lines[-1]+=c;width+=a
        assert len(lines)<=2
        top=174+22*len(lines)
        for step in (0,1,2,3):
            im=Image.new('RGB',(135,240),(8,12,8));im.paste(cover,(0,0))
            for j,line in enumerate(lines):text(im,line,(135-sum(glyph(c)[1] for c in line))//2,174+j*22)
            layer=Image.new('RGB',(135,240-top),(8,12,8));draw=ImageDraw.Draw(layer)
            discs=[]
            for k in (-1,0,1):
                if 0<=index+k<count:
                    angle=math.radians(40*(k+(1-step/3 if count>2 else 0)))
                    discs.append((abs(angle),k,67+round(60*math.sin(angle)),top+88-round(60*math.cos(angle)),angle))
            for _,k,x,y,angle in sorted(discs,reverse=True):
                yy=y-top;draw.ellipse((x-26,yy-26,x+26,yy+26),fill='black')
                for r in range(26,10,-4):draw.ellipse((x-r,yy-r,x+r,yy+r),outline=(56,56,56))
                draw.ellipse((x-9,yy-9,x+9,yy+9),fill=(248,124,0) if k==0 else (70,60,40))
                draw.ellipse((x-2,yy-2,x+2,yy+2),fill=(8,12,8))
                if k==0:
                    gs=[];total=0
                    for c in name:
                        g=glyph(c,True)
                        if total+g[1]>46:break
                        gs.append(g);total+=g[1]
                    at=0
                    for mask,a,dx,dy in gs:
                        theta=(at+a/2-total/2)/19+angle;at+=a
                        rot=mask.rotate(-math.degrees(theta),expand=True,resample=Image.Resampling.BICUBIC)
                        px=x+round(19*math.sin(theta))-rot.width//2;py=yy-round(19*math.cos(theta))-rot.height//2
                        if px>=0 and px+rot.width<=135 and py>=0 and py+rot.height<240-top:layer.paste('white',(px,py),rot)
            im.paste(layer,(0,top));stem=f'wheel-{count}-{len(lines)}line-{step}'
            im.save(PRIVATE/f'{stem}-135.png');im.resize((540,960),Image.Resampling.NEAREST).save(PRIVATE/f'{stem}-4x.png')
    print('Wheel previews generated (PC reference)',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--tags',action='store_true');parser.add_argument('--fonts',action='store_true');parser.add_argument('--previews',action='store_true');args=parser.parse_args()
    PRIVATE.mkdir(parents=True,exist_ok=True)
    if args.tags:tags()
    if args.fonts:fonts()
    if args.previews:previews()
