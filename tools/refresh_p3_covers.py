"""Regenerate only existing, mechanically bound song covers; no audio/LRC writes."""
import argparse,json,shutil
from pathlib import Path
from PIL import Image
from prepare_p3_media import PACKAGE,LOCAL,cover_ascii,validate_cover,sha

def build():
    report=[]
    for destination in sorted((PACKAGE/'ADVWalkman/covers').rglob('*.cover.adv')):
        relative=destination.relative_to(PACKAGE/'ADVWalkman/covers')
        base=relative.name.removesuffix('.cover.adv')
        directory=PACKAGE/'CoverSource'/relative.parent
        sources=[directory/(base+ext) for ext in ('.jpg','.png','.jpeg') if (directory/(base+ext)).is_file()]
        if len(sources)!=1:raise ValueError(f'Ambiguous/missing source: {relative}')
        with Image.open(sources[0]) as image:
            ratio=min(135/image.width,188/image.height)
            size=(min(135,round(image.width*ratio)),min(188,round(image.height*ratio)))
        preview,data,chars=cover_ascii(sources[0],40,32,size)
        assert validate_cover(data)==size
        destination.write_bytes(data)
        out=LOCAL/'previews/p3d-fix'/relative.parent;out.mkdir(parents=True,exist_ok=True)
        preview.save(out/(base+'.png'))
        preview.resize((size[0]*4,size[1]*4),Image.Resampling.NEAREST).save(out/(base+'-4x.png'))
        (out/(base+'.txt')).write_text(chars,encoding='ascii')
        report.append(dict(path=destination.relative_to(PACKAGE).as_posix(),size=len(data),dimensions=size,sha256=sha(destination)))
        print(relative,size,sha(destination),flush=True)
    (LOCAL/'p3d-fix-covers.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

def sync(sd):
    sd=Path(sd).resolve()
    if not (sd/'Music/AveMujica').is_dir():raise ValueError('Not the expected Walkman SD')
    report=json.loads((LOCAL/'p3d-fix-covers.json').read_text())
    for item in report:
        relative=Path(item['path']);target=(sd/relative).resolve()
        if not target.is_relative_to(sd/'ADVWalkman/covers') or not target.is_file():raise ValueError(target)
        source=PACKAGE/relative
        if sha(source)!=item['sha256']:raise ValueError('Package changed')
        shutil.copyfile(source,target)
        if sha(target)!=item['sha256']:raise ValueError('Copy verification failed')
    print('SD_COVERS_VERIFIED',len(report))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--build',action='store_true');p.add_argument('--sd');a=p.parse_args()
    if a.build:build()
    if a.sd:sync(a.sd)
