"""PC pixel/reference preview using the ACTUAL generated SD glyphs.

Not a claim of device validation. Mirrors six-column/current-cue page rules.
"""
import struct
from functools import lru_cache
from PIL import Image, ImageDraw
from prepare_p3_media import LOCAL,PACKAGE,parse_lrc,pair_cues

class Fonts:
    def __init__(self):
        self.records={};self.bitmaps={}
        for name in ('cjk-18','latin-14'):
            stem=PACKAGE/'ADVWalkman/fonts'/name
            idx=stem.with_suffix('.idx').read_bytes()
            self.records[name]={r[0]:r for r in (struct.unpack_from('<IIHHhhhHI',idx,i) for i in range(16,len(idx),24))}
            self.bitmaps[name]=stem.with_suffix('.vlw').read_bytes()

    def glyph(self,char):
        name='latin-14' if ord(char)<256 else 'cjk-18'
        _,offset,w,h,adv,dx,dy,px,_=self.records[name][ord(char)]
        mask=Image.frombytes('L',(max(w,1),max(h,1)),self.bitmaps[name][offset:offset+w*h]) if w*h else Image.new('L',(1,1))
        return mask,dx,dy,max(4,w,adv) if name=='latin-14' else 18

def columns(text,fonts):
    result=[[]] if text else [];y=0;in_word=False
    word=lambda c:c.isascii() and (c.isalnum() or c in "'-")
    for i,char in enumerate(text):
        mask,dx,dy,step=fonts.glyph(char)
        whole=0
        if word(char) and not in_word:
            for c in text[i:]:
                if not word(c) or whole>202:break
                whole+=fonts.glyph(c)[3]
        if y+step>202 or (y>0 and 0<whole<=202 and y+whole>202):result.append([]);y=0
        result[-1].append((char,y));y+=step
        in_word=word(char)
    return result

def layout(original,chinese,fonts,page=0):
    left,right=columns(original,fonts),columns(chinese,fonts)
    nl,nr=len(left),len(right);sl,sr=nl,nr
    if sl+sr>6:
        if not nr:sl,sr=6,0
        elif not nl:sl,sr=0,6
        elif nr<=3:sr,sl=nr,6-nr
        elif nl<=3:sl,sr=nl,6-nl
        else:sl,sr=3,3
    pl=(nl+sl-1)//sl if sl else 1;pr=(nr+sr-1)//sr if sr else 1
    pages=max(pl,pr);page=min(page,pages-1)
    width=(sl+sr)*19+(6 if sl and sr else 0)-1
    edge=67+max(width,0)//2
    glyphs=[]
    for block,slots,count,end in ((right,sr,pr,edge),(left,sl,pl,edge-sr*19-(6 if sr else 0))):
        first=min(page,count-1)*slots if count>1 else 0
        for col,values in enumerate(block[first:first+slots]):
            for char,y in values:glyphs.append((char,end-col*19-18,6+y))
    return glyphs,pages

def render(original,chinese,fonts,page=0,intro=False):
    image=Image.new('RGB',(135,240),'#080c08')
    draw=ImageDraw.Draw(image)
    draw.text((33,218),'0:00/4:59' if intro else '2:24/4:59',fill='white')
    draw.rectangle((6,220,8,229),fill='#ffcc00');draw.rectangle((11,220,13,229),fill='#ffcc00')
    draw.line((6,237,128,237),fill='#888888')
    glyphs,pages=layout(original,chinese,fonts,0 if intro else page)
    for char,x,y in glyphs:
        mask,dx,dy,_=fonts.glyph(char)
        if ord(char)<256 or char in '《》「」『』“”‘’（）—':mask=mask.transpose(Image.Transpose.ROTATE_270);dx=dy=0
        elif char in '，、。':dx,dy=18-mask.width,0
        assert 6<=x+dx and x+dx+mask.width<=129,(char,x,dx,mask.width)
        assert 6<=y+dy and y+dy+mask.height<=208,(char,y,dy,mask.height)
        image.paste((132,130,132) if intro else (255,255,255),(x+dx,y+dy),mask)
    return image,pages

if __name__=='__main__':
    fonts=Fonts()
    cues=pair_cues(parse_lrc((LOCAL/'crucifix-x.user.ja.lrc').read_bytes()),parse_lrc((LOCAL/'crucifix-x.zh-Hans.lrc').read_bytes()))
    for index,(ms,original,chinese) in enumerate(cues):
        image,pages=render(original,chinese,fonts)
        if index in (0,12,19):image.resize((540,960),Image.Resampling.NEAREST).save(LOCAL/f'previews/lyrics-{index:02}-4x.png')
        for page in range(pages):render(original,chinese,fonts,page)
    intro,_=render(cues[0][1],cues[0][2],fonts,intro=True)
    intro.resize((540,960),Image.Resampling.NEAREST).save(LOCAL/'previews/intro-complete-4x.png')
    print(f'{len(cues)} real lyric groups: all glyph bounds PASS')
