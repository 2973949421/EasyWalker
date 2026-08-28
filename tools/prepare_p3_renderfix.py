"""0.8.4 private font compiler and actual-glyph PC previews. No SD writes.

Only the four library faces are replaced. Music, lyrics, covers and all other
fonts are inputs, not outputs. Original replacement files remain recoverable.
"""
import argparse,json,math,shutil
from pathlib import Path
from PIL import Image,ImageDraw
from prepare_p3_media import LOCAL,PACKAGE,make_font,font_sources,sha
from prepare_p3_refine import source,records

PRIVATE=LOCAL/'renderfix-084'
FACES=('library-cjk-12','library-cjk-18','library-latin-14','library-latin-22')

def fonts():
    target=PACKAGE/'ADVWalkman/fonts';reports=[]
    for name in FACES:
        stem=target/name
        for ext in ('.vlw','.idx','.idx2'):
            before=stem.with_suffix(ext);saved=PRIVATE/'recovery'/before.relative_to(PACKAGE)
            saved.parent.mkdir(parents=True,exist_ok=True)
            if not saved.exists():shutil.copy2(before,saved)
        known=records(stem)
        latin='latin' in name
        primary=source('C:/Windows/Fonts/'+('KUNSTLER.TTF' if latin else 'STXINGKA.TTF'))
        print('Emboldening',name,flush=True)
        report=make_font(name,int(name.split('-')[-1]),known,[primary,*font_sources(latin)],target,embolden=True)
        assert not report['missing']
        assert set(records(stem))==set(known)
        report['files']={ext:sha(stem.with_suffix(ext)) for ext in ('.vlw','.idx','.idx2')}
        reports.append(report)
    (PRIVATE/'fonts.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def previews():
    faces={name:(records(PACKAGE/'ADVWalkman/fonts'/name),(PACKAGE/'ADVWalkman/fonts'/name).with_suffix('.vlw').read_bytes()) for name in FACES}
    def glyph(c,small=False):
        latin=ord(c)<256;size=(14 if latin else 12) if small else (22 if latin else 18)
        rs,bits=faces[f'library-{"latin" if latin else "cjk"}-{size}']
        _,off,w,h,adv,dx,dy,_,_=rs[ord(c)]
        # The runtime converts 8-bit coverage to 4-bit on load.
        mask=Image.frombytes('L',(w,h),bits[off:off+w*h]) if w*h else Image.new('L',(1,1))
        mask=mask.point(lambda a:min(15,(a+8)//17)*17)
        return mask,adv,dx,dy
    cover=Image.open(LOCAL/'p3d/library-cover-135.png').convert('RGB')
    cases=[('empty',[],0),('one',['AveMujica'],0),('two',['AveMujica','未分类'],0),
           ('two-cn',['AveMujica','未分类'],1),('three',['未分类','AveMujica','华文行楷音乐收藏'],1),
           ('long-cn',['AveMujica','华文行楷音乐收藏','未分类'],1),
           ('long-en',['未分类','AveMujica Music Collection','华文行楷音乐收藏'],1)]
    report=[]
    for case,names,index in cases:
        title=names[index] if names else '暂无曲库';advance=left=right=0
        for c in title:
            mask,a,dx,dy=glyph(c);left=min(left,advance+dx);right=max(right,advance+dx+mask.width);advance+=a
        width=max(right,advance)-left
        offsets=sorted({0,max(0,width-123)//2,max(0,width-123)})
        for offset in offsets:
            for step in (0,1,2,3):
                im=Image.new('RGB',(135,240),(8,12,8));im.paste(cover,(0,0))
                band=Image.new('RGB',(123,22),(8,12,8))
                x=(135-width)//2-left-6 if width<=123 else -left-offset
                for c in title:
                    mask,a,dx,dy=glyph(c);band.paste('white',(x+dx,dy),mask);x+=a
                im.paste(band,(6,174))
                layer=Image.new('RGB',(135,44),(8,12,8));draw=ImageDraw.Draw(layer);poses=[]
                for k in (-1,0,1) if names else ():
                    angle=(k+(1-step/3 if len(names)>1 else 0))*math.radians(40)
                    poses.append((abs(angle),k,67+round(60*math.sin(angle)),88-round(60*math.cos(angle)),angle))
                labels=[]
                for _,k,x,y,angle in sorted(poses,reverse=True):
                    name=names[(index+k)%len(names)];labels.append(name)
                    draw.ellipse((x-26,y-26,x+26,y+26),fill='black')
                    for r in range(26,10,-4):draw.ellipse((x-r,y-r,x+r,y+r),outline=(48,48,48) if k==0 else (24,24,24))
                    draw.ellipse((x-9,y-9,x+9,y+9),fill=(248,124,0) if k==0 else (66,65,66))
                    draw.ellipse((x-2,y-2,x+2,y+2),fill=(8,12,8))
                    gs=[];total=0
                    for c in name[:10]:
                        g=glyph(c,True);a=max(g[1],4)
                        if total+a>46:break
                        gs.append((g,a));total+=a
                    cursor=0
                    for (mask,_,dx,dy),a in gs:
                        theta=(cursor+a/2-total/2)/19+angle;cursor+=a
                        co,si=math.cos(theta),math.sin(theta)
                        px=x+round(19*si);py=y-round(19*co)
                        hw=math.ceil((mask.width*abs(co)+mask.height*abs(si))/2)
                        hh=math.ceil((mask.height*abs(co)+mask.width*abs(si))/2)
                        if px-hw<0 or px+hw>=135 or py-hh<0 or py+hh>=44:continue
                        color=(99,190,255) if k==0 else (66,121,189)
                        for j in range(mask.height):
                            for i in range(mask.width):
                                alpha=mask.getpixel((i,j))
                                xx=px+round((i-mask.width//2)*co-(j-mask.height//2)*si)
                                yy=py+round((i-mask.width//2)*si+(j-mask.height//2)*co)
                                if alpha and 0<=xx<135 and 0<=yy<44:
                                    base=layer.getpixel((xx,yy));layer.putpixel((xx,yy),tuple((b*(255-alpha)+f*alpha)//255 for b,f in zip(base,color)))
                im.paste(layer,(0,196));stem=f'{case}-offset{offset}-step{step}'
                im.save(PRIVATE/f'{stem}-135.png');im.resize((540,960),Image.Resampling.NEAREST).save(PRIVATE/f'{stem}-4x.png')
                report.append(dict(name=stem,labels=labels,count=len(names),offset=offset,name_width=width,wheel_top=196))
    (PRIVATE/'previews.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Actual-glyph PC preview cases:',len(report),flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fonts',action='store_true');parser.add_argument('--previews',action='store_true');args=parser.parse_args()
    PRIVATE.mkdir(parents=True,exist_ok=True)
    if args.fonts:fonts()
    if args.previews:previews()
