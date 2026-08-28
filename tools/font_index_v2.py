"""Direct BMP/Latin index sidecar. VLW and v1 index remain backward compatible."""
import struct,zlib
from pathlib import Path

def generate(index: Path):
    old=index.read_bytes()
    magic,version,stride,count,vlw_size=struct.unpack_from('<4sHHII',old)
    if (magic,version,stride)!=(b'FIDX',1,24) or len(old)!=16+count*24:
        raise ValueError(f'Invalid v1 index: {index}')
    if index.with_suffix('.vlw').stat().st_size!=vlw_size:raise ValueError('VLW size mismatch')
    slots=256 if index.stem.startswith('latin-') else 65536
    out=bytearray(512+slots*16)
    struct.pack_into('<4sHHII',out,0,b'FIDX',2,16,slots,vlw_size)
    with index.with_suffix('.vlw').open('rb') as font:
        struct.pack_into('<I',out,16,zlib.crc32(font.read(24)))
    for at in range(16,len(old),24):
        cp,offset,w,h,advance,dx,dy,_,_=struct.unpack_from('<IIHHhhhHI',old,at)
        if cp>=slots or not offset or offset+w*h>vlw_size:raise ValueError('Invalid glyph')
        struct.pack_into('<IHHhhhH',out,512+cp*16,offset,w,h,advance,dx,dy,0)
    target=index.with_suffix('.idx2');target.write_bytes(out)
    return target

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);args=p.parse_args()
    for path in sorted(args.directory.glob('*.idx')):print(generate(path))
