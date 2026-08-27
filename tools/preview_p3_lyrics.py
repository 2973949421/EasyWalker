"""PC pixel/reference preview using the ACTUAL generated SD glyphs.

Not a claim of device validation. Mirrors six-column/page rules for layout QA.
"""
import struct
from functools import lru_cache
from PIL import Image, ImageDraw
from prepare_p3_media import LOCAL,PACKAGE,parse_lrc,pair_cues

class Fonts:
    def __init__(self):
        self.records={};self.bitmaps={}
        for name in ('cjk-16','latin-12'):
            stem=PACKAGE/'ADVWalkman/fonts'/name
            idx=stem.with_suffix('.idx').read_bytes()
            self.records[name]={r[0]:r for r in (struct.unpack_from('<IIHHhhhHI',idx,i) for i in range(16,len(idx),24))}
            self.bitmaps[name]=stem.with_suffix('.vlw').read_bytes()

    def glyph(self,char):
        name='latin-12' if ord(char)<256 else 'cjk-16'
        _,offset,w,h,adv,dx,dy,px,_=self.records[name][ord(char)]
        mask=Image.frombytes('L',(max(w,1),max(h,1)),self.bitmaps[name][offset:offset+w*h]) if w*h else Image.new('L',(1,1))
        return mask,dx,dy,max(4,w,adv) if name=='latin-12' else 16

def columns(text,fonts):
    result=[[]] if text else [];y=0
    for char in text:
        mask,dx,dy,step=fonts.glyph(char)
        if y+step>160:result.append([]);y=0
        result[-1].append((char,y));y+=step
    return result

def layout(original,chinese,fonts,page=0):
    left,right=columns(original,fonts),columns(chinese,fonts)
    nl,nr=len(left),len(right);sl,sr=nl,nr
    if sl+sr>6:
        if not nr:sl,sr=6,0
        elif not nl:sl,sr=0,6
        elif nr<=3:sr,sl=nr,6-nr
        elif nl<=3:sl,sr=nl,6-nl
        else:sl=sr=3
    pl=(nl+sl-1)//sl if sl else 1;pr=(nr+sr-1)//sr if sr else 1
    pages=max(pl,pr);page=min(page,pages-1)
    width=(sl+sr)*18+(6 if sl and sr else 0)-2
    edge=67+max(width,0)//2
    glyphs=[]
    for block,slots,count,end in ((right,sr,pr,edge),(left,sl,pl,edge-sr*18-(6 if sr else 0))):
        first=min(page,count-1)*slots if count>1 else 0
        for col,values in enumerate(block[first:first+slots]):
            for char,y in values:glyphs.append((char,end-col*18-16,4+y))
    return glyphs,pages

def render(original,chinese,fonts,page=0):
    image=Image.new('RGB',(135,240),'#080c08')
    draw=ImageDraw.Draw(image);draw.line((0,33,134,33),fill='#888888');draw.line((0,202,134,202),fill='#888888')
    glyphs,pages=layout(original,chinese,fonts,page)
    for char,x,y in glyphs:
        mask,dx,dy,_=fonts.glyph(char)
        if ord(char)<256:mask=mask.transpose(Image.Transpose.ROTATE_270);dx=dy=0
        assert 6<=x+dx and x+dx+mask.width<=129,(char,x,dx,mask.width)
        assert 4<=y+dy and y+dy+mask.height<=164,(char,y,dy,mask.height)
        image.paste((255,255,255),(x+dx,34+y+dy),mask)
    return image,pages

if __name__=='__main__':
    fonts=Fonts()
    cues=pair_cues(parse_lrc((LOCAL/'crucifix-x.user.ja.lrc').read_bytes()),parse_lrc((LOCAL/'crucifix-x.zh-Hans.lrc').read_bytes()))
    for index,(ms,original,chinese) in enumerate(cues):
        image,pages=render(original,chinese,fonts)
        if index in (0,12,19):image.resize((540,960),Image.Resampling.NEAREST).save(LOCAL/f'previews/lyrics-{index:02}-4x.png')
        for page in range(pages):render(original,chinese,fonts,page)
    print(f'{len(cues)} real lyric groups: all glyph bounds PASS')
