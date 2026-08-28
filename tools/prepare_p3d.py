"""P3D ordinary library cover compiler. Private output; never touches music/SD."""
import argparse
import hashlib
import math
import struct
import urllib.request
import zlib
from pathlib import Path
from PIL import Image, ImageOps, ImageDraw
from prepare_p3_media import LOCAL, PACKAGE

SOURCE = ('https://anime.bang-dream.com/avemujica/wordpress/wp-content/themes/'
          'avemujica_0102/assets/images/common/index/img_hero_2.jpg')
DEST = Path('ADVWalkman/library-covers/folders/AveMujica/cover.adv')

def compile_cover(source):
    picture=ImageOps.exif_transpose(Image.open(source)).convert('RGB')
    # Keep all five members. Empty space is intentional, not a crop.
    picture=ImageOps.contain(picture,(120,120),Image.Resampling.LANCZOS)
    canvas=Image.new('RGB',(120,120),(8,12,8))
    canvas.paste(picture,((120-picture.width)//2,(120-picture.height)//2))
    pixels=b''.join(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b in canvas.get_flattened_data())
    return canvas,struct.pack('<4s6H2I',b'LCOV',1,24,120,120,1,0,len(pixels),zlib.crc32(pixels))+pixels

def validate_cover(data):
    if len(data)!=28824:raise ValueError('library_cover_length')
    m,v,n,w,h,fmt,res,length,crc=struct.unpack('<4s6H2I',data[:24])
    if (m,v,n,w,h,fmt,res,length)!=(b'LCOV',1,24,120,120,1,0,28800):raise ValueError('library_cover_header')
    if zlib.crc32(data[24:])!=crc:raise ValueError('library_cover_crc')
    return w,h

def font_coverage():
    # All normal UI Chinese literals, not just song-specific vocabulary.
    root=Path(__file__).resolve().parents[1]
    required=set()
    for p in (root/'src/player/ui').rglob('*.cpp'):
        # Comments may contain more words; checking them is harmless with the
        # existing full CJK repertoire. No font regeneration if already present.
        required.update(ord(c) for c in p.read_text(encoding='utf-8') if '\u3000'<=c<='\uffef')
    for face in ('cjk-12','cjk-14'):
        data=(PACKAGE/f'ADVWalkman/fonts/{face}.idx').read_bytes()
        known={struct.unpack_from('<I',data,i)[0] for i in range(16,len(data),24)}
        if required-known:raise ValueError(f'{face} missing {required-known}')
    return len(required)

def preview_ui(cover,private):
    """Pixel-layout reference using delivered glyphs, NOT firmware execution."""
    font_data={}
    for face in ('latin-12','cjk-12'):
        base=PACKAGE/'ADVWalkman/fonts'/face;idx=base.with_suffix('.idx').read_bytes()
        font_data[face]=({r[0]:r for r in (struct.unpack_from('<IIHHhhhHI',idx,i) for i in range(16,len(idx),24))},base.with_suffix('.vlw').read_bytes())
    def glyph(c):
        records,bits=font_data['latin-12' if ord(c)<256 else 'cjk-12'];cp,off,w,h,advance,dx,dy,px,_=records[ord(c)]
        mask=Image.frombytes('L',(w,h),bits[off:off+w*h]) if w*h else Image.new('L',(1,1))
        return mask,advance,dx,dy
    def text(im,s,x,y,color=(240,240,240)):
        for c in s:
            mask,advance,dx,dy=glyph(c);im.paste(color,(x+dx,y+dy),mask);x+=advance
        return x
    def disc(im,x,y,r,selected):
        d=ImageDraw.Draw(im);d.ellipse((x-r,y-r,x+r,y+r),fill='black')
        for rr in range(r,10,-4):d.ellipse((x-rr,y-rr,x+rr,y+rr),outline=(48,48,48) if selected else (24,24,24))
        d.ellipse((x-9,y-9,x+9,y+9),fill=(248,124,0) if selected else (64,64,64));d.ellipse((x-2,y-2,x+2,y+2),fill=(8,12,8))
    for name,index in [('AveMujica',1),('ADVWalkmanBenchmark',0)]:
        im=Image.new('RGB',(135,240),(8,12,8))
        if index:im.paste(cover,(7,6))
        else:disc(im,67,66,32,False)
        text(im,'AveMujica' if index else 'ADVWalkman',19,130)
        if not index:text(im,'Benchmark',19,145)
        disc(im,24 if index else 110,202,26,False);disc(im,67,195,28,True)
        chars=[];total=0
        for c in name[:10]:
            g=glyph(c)
            if total+g[1]>64:break
            chars.append(g);total+=g[1]
        cursor=0
        for mask,advance,dx,dy in chars:
            angle=(cursor+advance/2-total/2)/24;cursor+=advance
            rotated=mask.rotate(-math.degrees(angle),expand=True,resample=Image.Resampling.BICUBIC)
            x=round(67+24*math.sin(angle)-rotated.width/2);y=round(195-24*math.cos(angle)-rotated.height/2)
            im.paste((248,248,248),(x,y),rotated)
        text(im,f'{index+1}/2',4,211,(248,124,0));text(im,'左右选择 S设置',3,227)
        im.save(private/f'layout-{name}-135.png');im.resize((405,720),Image.Resampling.NEAREST).save(private/f'layout-{name}-3x.png')
    im=Image.new('RGB',(135,240),(8,12,8));d=ImageDraw.Draw(im)
    text(im,'设置',6,7,(248,124,0));d.line((6,25,129,25),fill=(128,128,128))
    d.rectangle((3,39,131,62),outline=(248,124,0))
    for i,s in enumerate(('屏幕亮度 70%','息屏时间','关于','返回 Launcher')):text(im,s,6,44+i*31)
    text(im,'已保存',6,187);text(im,'上下选择 左右调整',6,210);text(im,'Esc 返回',6,226)
    im.save(private/'layout-settings-135.png');im.resize((405,720),Image.Resampling.NEAREST).save(private/'layout-settings-3x.png')

def main(download):
    private=LOCAL/'p3d';private.mkdir(parents=True,exist_ok=True)
    source=private/'AveMujica.official-five-characters.jpg'
    if download:
        request=urllib.request.Request(SOURCE,headers={'User-Agent':'ADVWalkman-resource/1.0'})
        with urllib.request.urlopen(request,timeout=40) as response:data=response.read(12*1024*1024+1)
        if len(data)>12*1024*1024:raise ValueError('source_too_large')
        source.write_bytes(data)
    preview,data=compile_cover(source);validate_cover(data)
    target=PACKAGE/DEST;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    preview.save(private/'library-cover-120.png')
    preview.resize((480,480),Image.Resampling.NEAREST).save(private/'library-cover-4x.png')
    preview_ui(preview,private)
    report=f'Source: {SOURCE}\nSource SHA256: {hashlib.sha256(source.read_bytes()).hexdigest()}\nOutput: {DEST.as_posix()}\nOutput SHA256: {hashlib.sha256(data).hexdigest()}\nUI CJK coverage: {font_coverage()}\nExisting fonts unchanged. Music, lyrics, song covers and SD untouched.\n'
    (private/'resource-report.txt').write_text(report,encoding='utf-8');print(report)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--download',action='store_true')
    main(parser.parse_args().download)
