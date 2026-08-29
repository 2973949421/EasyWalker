"""Deterministic, local-only P3 media compiler. Never modifies source music.

Outputs private fonts, LRC and real glyph-based RGB565 ASCII covers under
test-data/local/p3-media/package. No audio, fonts or art enter Git.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import struct
import subprocess
import urllib.request
import zlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps, ImageEnhance, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / 'test-data/local/p3-media'
PACKAGE = LOCAL / 'package'
COVER_PAGE = 'https://bushiroad-music.com/musics/crucifixx/'
COVER_URL = ('https://bushiroad-music.com/wordpress/wp-content/uploads/2025/03/'
             '06224444/%E3%80%90JK%E3%80%91Ave-Mujica_CrucifixX_1000.jpg')
RAMP = ''.join(chr(cp) for cp in range(32,127))
GRIDS = [(26,20),(30,24),(34,26),(40,32),(48,40)]
STAMP = re.compile(r'\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\]')


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_lrc(data: bytes):
    if len(data) > 128 * 1024:
        raise ValueError('lrc_file_limit')
    text = data.decode('utf-8-sig', errors='strict')
    if '\x00' in text:raise ValueError('lrc_nul')
    offset = 0
    cues = []
    for line in text.splitlines():
        if len(line.encode('utf-8')) > 1024:
            raise ValueError('lrc_line_limit')
        match = re.fullmatch(r'\[offset:([+-]?\d+)\]', line.strip(), re.I)
        if match:
            offset = int(match[1])
            continue
        matches = list(STAMP.finditer(line))
        if not matches:
            continue
        lyric = line[matches[-1].end():].strip()
        for match in matches:
            minute, second, frac = match.groups()
            if int(second) >= 60:
                raise ValueError('lrc_timestamp')
            cues.append(((int(minute)*60+int(second))*1000 +
                         int((frac or '').ljust(3, '0')), lyric))
        if len(cues) > 512:
            raise ValueError('lrc_cue_limit')
    return sorted([(max(0, ms+offset), text) for ms, text in cues], key=lambda c: c[0])


def pair_cues(original, translated):
    unused = set(range(len(translated)))
    result = []
    for ms, text in original:
        possible = [i for i in unused if abs(translated[i][0]-ms) <= 300]
        best = min(possible, key=lambda i: (abs(translated[i][0]-ms), translated[i][0], i)) if possible else None
        result.append((ms, text, translated[best][1] if best is not None else ''))
        if best is not None:
            unused.remove(best)
    return result


def font_sources(latin=False):
    # Keep report-only validators usable on the firmware/toolchain Python.
    # The optional dependency is required only when fonts are regenerated.
    from fontTools.ttLib import TTFont
    names = ['times.ttf', 'seguisym.ttf'] if latin else ['simkai.ttf', 'msgothic.ttc', 'simsun.ttc', 'seguisym.ttf']
    sources = []
    for name in names:
        path = Path('C:/Windows/Fonts') / name
        if not path.exists():
            continue
        font = TTFont(path, fontNumber=0, lazy=True)
        cmap = font.getBestCmap() or {}
        sources.append((path, set(cmap)))
        font.close()
    return sources


def make_font(name: str, size: int, codepoints, sources, target: Path, *, embolden=False):
    target.mkdir(parents=True, exist_ok=True)
    glyphs, missing, fallbacks = [], [], {}
    scale=4
    loaded = [ImageFont.truetype(str(path), size*scale, index=0) for path, _ in sources]
    for cp in sorted(codepoints):
        selection = next((i for i, (_, cmap) in enumerate(sources) if cp in cmap), None)
        if selection is None:
            missing.append(cp)
            continue
        if selection:
            fallbacks[cp] = sources[selection][0].name
        font = loaded[selection]
        char = chr(cp)
        left, top, right, bottom = font.getbbox(char, anchor='ls')
        width, height = max(0, right-left), max(0, bottom-top)
        bitmap = Image.new('L', (max(1, width), max(1, height)))
        ImageDraw.Draw(bitmap).text((-left, -top), char, font=font, fill=255, anchor='ls')
        if embolden and width and height:
            # Expand the high-resolution canvas before dilation: preserve
            # italic overhangs instead of clipping the added outer stroke.
            bitmap=ImageOps.expand(bitmap,border=1).filter(ImageFilter.MaxFilter(3))
            left-=1;top-=1;width+=2;height+=2
        if name=='cjk-18':bitmap=bitmap.filter(ImageFilter.MaxFilter(3))
        if width and height:
            bitmap=bitmap.resize((max(1,math.ceil(width/scale)),max(1,math.ceil(height/scale))),Image.Resampling.LANCZOS)
            width,height=bitmap.size
        left=round(left/scale);top=round(top/scale)
        advance = max(1,round(font.getlength(char)/scale))
        if not width or not height:width=height=0;bitmap=Image.new('L',(1,1))
        if name.startswith('cjk-'):
            # A few KaiTi overhangs rasterize to 17px at nominal 16px. Fit the
            # WHOLE outline into its fixed em cell instead of clipping a stroke
            # or allowing a metric to change when a cache entry is missing.
            if width>size or height>size:
                bitmap=ImageOps.contain(bitmap,(size,size),Image.Resampling.LANCZOS)
                width,height=bitmap.size
            left=(size-width)//2
            advance=size
        if width>size or height>size:
            bitmap=ImageOps.contain(bitmap,(size,size),Image.Resampling.LANCZOS);width,height=bitmap.size
        dy=(size-height)//2 if name.startswith('cjk-') else max(0,min(size-height,round(size*.78)+top))
        # The index specifies a cell-top origin. Latin uses a common baseline.
        # the conventional ascent-relative metrics for third-party inspection.
        glyphs.append((cp, width, height, advance, left, dy, -top,
                       bitmap.tobytes() if width and height else b''))
    vlw = bytearray(struct.pack('>6I', len(glyphs), 11, size, 0, size, 0))
    for cp, w, h, adv, dx, dy, ascent, bitmap in glyphs:
        vlw.extend(struct.pack('>7i', cp, h, w, adv, ascent, dx, 0))
    index = bytearray()
    offset = len(vlw)
    for cp, w, h, adv, dx, dy, ascent, bitmap in glyphs:
        index.extend(struct.pack('<IIHHhhhHI', cp, offset, w, h, adv, dx, dy, size, 0))
        vlw.extend(bitmap)
        offset += len(bitmap)
    stem = target / name
    stem.with_suffix('.vlw').write_bytes(vlw)
    stem.with_suffix('.idx').write_bytes(struct.pack('<4sHHII', b'FIDX', 1, 24, len(glyphs), len(vlw)) + index)
    from font_index_v2 import generate
    generate(stem.with_suffix('.idx'))
    return {'name': name, 'glyphs': len(glyphs), 'bytes': len(vlw), 'missing': missing, 'fallbacks': fallbacks,
            'embolden_high_res_radius':1 if embolden else 0,'raster_scale':scale}


def cover_ascii(source: Path, columns: int, rows: int, canvas_size=(120,144)):
    # Each source tile is approximated by a rasterized character mask. Colour
    # comes from the source, while coverage/shape comes ONLY from the glyph.
    original = Image.open(source).convert('RGB')
    width,height=canvas_size
    if not (0<width<=135 and 0<height<=188):raise ValueError('cover_dimensions')
    canvas = Image.new('RGB', canvas_size, '#080c08')
    fitted = ImageOps.contain(original, canvas_size, Image.Resampling.LANCZOS)
    fitted = ImageEnhance.Contrast(fitted).enhance(1.08).filter(ImageFilter.UnsharpMask(radius=.7,percent=110,threshold=3))
    ox, oy = (width-fitted.width)//2, (height-fitted.height)//2
    original_canvas = canvas.copy()
    original_canvas.paste(fitted, (ox, oy))
    font = ImageFont.truetype('C:/Windows/Fonts/times.ttf', 12)
    masks = []
    for char in RAMP:
        mask = Image.new('L', (10, 16))
        ImageDraw.Draw(mask).text((0, -1), char, font=font, fill=255)
        # Remove unused font-cell margins, not per-character bounding boxes:
        # punctuation stays small while glyph strokes use the limited pixels.
        masks.append(mask.crop((0,1,8,13)))
    samples=[list(mask.resize((4,6),Image.Resampling.LANCZOS).get_flattened_data()) for mask in masks]
    densities=[sum(small)/len(small)/255 for small in samples]
    # Long rules/underscores should represent an actual edge, not win every
    # bright flat tile and turn the picture into horizontal scanlines.
    rules=set('_-=~|')
    chars = []
    for row in range(rows):
        line = ''
        for col in range(columns):
            x0, x1 = col*width//columns, (col+1)*width//columns
            y0, y1 = row*height//rows, (row+1)*height//rows
            if x1 <= ox or x0 >= ox+fitted.width or y1 <= oy or y0 >= oy+fitted.height:
                line += ' '
                continue
            tile = original_canvas.crop((x0, y0, x1, y1))
            luminance = list(tile.convert('L').resize((4, 6)).get_flattened_data())
            level = sum(luminance)/len(luminance)/255
            candidate_scores = []
            for n, mask in enumerate(masks):
                small = samples[n]
                density = densities[n]
                # Normalized spatial contrast plus overall glyph density.
                shape = sum(((a/255-level)-(b/255-density))**2 for a,b in zip(luminance,small))/24
                edge=sum(abs((luminance[i]-luminance[i-1])-(small[i]-small[i-1])) for i in range(1,24) if i%4)/255/18
                contrast=(max(luminance)-min(luminance))/255
                line_penalty=.25*(1-contrast) if RAMP[n] in rules else 0
                candidate_scores.append(shape + 1.8*(density-math.sqrt(level)*0.65)**2 + .15*edge+line_penalty)
            chosen = min(range(len(RAMP)), key=candidate_scores.__getitem__)
            line += RAMP[chosen]
            rgb = tile.resize((1, 1), Image.Resampling.BOX).getpixel((0, 0))
            canvas.paste(tuple(round(v*.25) for v in rgb),(x0,y0,x1,y1))
            # Compensate glyph negative space without converting to a bitmap.
            rgb = tuple(min(255, round(v*1.35)) for v in rgb)
            mask = ImageOps.autocontrast(masks[chosen].resize((x1-x0, y1-y0), Image.Resampling.LANCZOS))
            canvas.paste(rgb, (x0, y0, x1, y1), mask)
        chars.append(line)
    payload = b''.join(struct.pack('<H', ((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b in canvas.get_flattened_data())
    header = struct.pack('<4sHHHHHHHHII', b'ACOV', 1, 28, width, height, columns, rows, 1, 0,
                         len(payload), zlib.crc32(payload))
    return canvas, header+payload, '\n'.join(chars)


def validate_cover(data: bytes):
    if len(data) < 28:
        raise ValueError('cover_header')
    magic, version, header, w, h, cols, rows, pixel_format, reserved, length, crc = struct.unpack('<4sHHHHHHHHII', data[:28])
    if (magic, version, header, pixel_format, reserved) != (b'ACOV', 1, 28, 1, 0) or not (0<w<=135 and 0<h<=188) or (cols, rows) not in GRIDS:
        raise ValueError('cover_format')
    if length != w*h*2 or len(data) != 28+length or zlib.crc32(data[28:]) != crc:
        raise ValueError('cover_payload')
    return w, h


def build(download=False, fonts=False):
    LOCAL.mkdir(parents=True, exist_ok=True)
    source = LOCAL / 'Crucifix-X.official-single.jpg'
    if download:
        request = urllib.request.Request(COVER_URL, headers={'User-Agent': 'ADVWalkman-media/1.0'})
        with urllib.request.urlopen(request, timeout=40) as response:
            data = response.read(12*1024*1024+1)
        if len(data) > 12*1024*1024:
            raise ValueError('cover_download_limit')
        source.write_bytes(data)
    if not source.exists():
        raise FileNotFoundError('Run --download once for the selected official cover')
    Image.open(source).verify()
    lyrics = PACKAGE / 'Lyrics/ADVWalkmanBenchmark'
    lyrics.mkdir(parents=True, exist_ok=True)
    original = LOCAL / 'crucifix-x.user.ja.lrc'
    translation = LOCAL / 'crucifix-x.zh-Hans.lrc'
    a, b = parse_lrc(original.read_bytes()), parse_lrc(translation.read_bytes())
    shutil.copyfile(original, lyrics / 'benchmark.lrc')
    shutil.copyfile(translation, lyrics / 'benchmark.zh-Hans.lrc')
    cover_dir = PACKAGE / 'ADVWalkman/covers/ADVWalkmanBenchmark'
    cover_dir.mkdir(parents=True, exist_ok=True)
    preview_dir = LOCAL / 'previews'
    preview_dir.mkdir(exist_ok=True)
    for cols, rows in [(34,26),(40,32),(48,40)]:
        preview, data, chars = cover_ascii(source, cols, rows)
        validate_cover(data)
        preview.save(preview_dir / f'crucifix-x-{cols}x{rows}.png')
        preview.resize((480,576), Image.Resampling.NEAREST).save(preview_dir / f'crucifix-x-{cols}x{rows}-4x.png')
        (preview_dir / f'crucifix-x-{cols}x{rows}.txt').write_text(chars, encoding='utf-8')
        if (cols, rows) == (40,32):
            (cover_dir / 'benchmark.cover.adv').write_bytes(data)
    source_dir = PACKAGE / 'CoverSource/ADVWalkmanBenchmark'
    source_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, source_dir / 'benchmark.jpg')
    fixture=PACKAGE/'Music/ADVWalkmanP3Test'
    fixture.mkdir(parents=True,exist_ok=True)
    (fixture/'.adv-walkman-p3-fixture').write_text('ADV-Walkman P3C generated audio v1\n',encoding='ascii')
    sound=fixture/'no-lyrics.mp3'
    if not sound.exists():
        ffmpeg=Path('B:/Tools/FFmpeg/ffmpeg-9.0.1-essentials_build/bin/ffmpeg.exe')
        subprocess.run([str(ffmpeg),'-v','error','-f','lavfi','-i',
                        'aevalsrc=0.01*sin(2*PI*440*t)|0.01*sin(2*PI*660*t):s=44100:d=6',
                        '-codec:a','libmp3lame','-b:a','128k','-metadata','title=Generated P3 no lyrics',str(sound)],check=True)
    report = ['# Private P3 resource report', '', f'Source: {COVER_PAGE}', f'Image: {COVER_URL}',
              f'Cover SHA-256: {sha(source)}', f'LRC: original={len(a)} translated={len(b)} paired={sum(bool(c[2]) for c in pair_cues(a,b))}',
              'User Japanese bytes preserved; no MP3 modified; fonts for local use only.']
    if fonts:
        required = set(map(ord, ''.join(t for _,t in a+b) + '前奏未分类暂无歌词缺少字体资源'))
        for lyric in (PACKAGE/'Lyrics').rglob('*.lrc'):
            required.update(ord(c) for _,t in parse_lrc(lyric.read_bytes()) for c in t)
        # UI metadata is part of the font contract too (e.g. Symbol I △ / III ▽).
        ffprobe=Path('B:/Tools/FFmpeg/ffmpeg-9.0.1-essentials_build/bin/ffprobe.exe')
        for audio in (PACKAGE/'Music').rglob('*.mp3'):
            result=subprocess.run([str(ffprobe),'-v','error','-show_entries',
                'format_tags=title,artist,album','-of','json',str(audio)],check=True,capture_output=True)
            tags=json.loads(result.stdout).get('format',{}).get('tags',{})
            required.update(ord(c) for text in tags.values() for c in text)
        cjk = font_sources()
        # BMP CJK / punctuation / kana coverage is reusable, not song-specific.
        repertoire = set(range(0x3000, 0xA000)) | set(range(0xFF00, 0xFFF0)) | required
        repertoire -= set(range(0x20, 0x100))
        for size in (12,14,16,18):
            item = make_font(f'cjk-{size}', size, repertoire, cjk, PACKAGE / 'ADVWalkman/fonts')
            missing_required = required.intersection(item['missing'])
            if missing_required:
                raise ValueError(f'missing_required_glyphs={sorted(missing_required)}')
            report.append(f"{item['name']}: glyphs={item['glyphs']} bytes={item['bytes']} unsupported_BMP={len(item['missing'])} fallback={len(item['fallbacks'])}")
            (LOCAL / f"{item['name']}-fallback.tsv").write_text('\n'.join(f'U+{cp:04X}\t{font}' for cp,font in item['fallbacks'].items()), encoding='utf-8')
        for size in (10,12,14):
            latin = make_font(f'latin-{size}', size, set(range(0x20,0x100)), font_sources(True), PACKAGE / 'ADVWalkman/fonts')
            report.append(f"latin-{size}: glyphs={latin['glyphs']} missing={latin['missing']}")
    (LOCAL / 'RESOURCE_REPORT.md').write_text('\n'.join(report)+'\n', encoding='utf-8')
    refresh_manifest()
    print('\n'.join(report))
    print(f'PACKAGE={PACKAGE}')


def refresh_manifest():
    """Finalize after both benchmark and song-batch preparation; no SD access."""
    (LOCAL/'PACKAGE.sha256').write_text(''.join(f'{sha(path)}  {path.relative_to(PACKAGE).as_posix()}\n' for path in sorted(PACKAGE.rglob('*')) if path.is_file()),encoding='utf-8')
    report=['# Generated private font inventory','',
            'Windows KaiTi / Times New Roman; local use, no redistribution license inferred.',
            '4x raster then downsample; CJK18 slight emboldening; SD8-bit / RAM4-bit coverage.',
            'Actual lyric coverage and cache limits are checked by check_p3_closure.py.','',
            '| Face | Glyphs | VLW bytes | IDX bytes | Fallback glyphs |',
            '|---|---:|---:|---:|---:|']
    for index in sorted((PACKAGE/'ADVWalkman/fonts').glob('*.idx')):
        count=struct.unpack_from('<I',index.read_bytes(),8)[0]
        fallback=LOCAL/(index.stem+'-fallback.tsv')
        report.append(f'| {index.stem} | {count} | {index.with_suffix(".vlw").stat().st_size} | {index.stat().st_size} | {len(fallback.read_text(encoding="utf-8").splitlines()) if fallback.exists() else 0} |')
    (LOCAL/'FONT_REPORT.md').write_text('\n'.join(report)+'\n',encoding='utf-8')
    print('PACKAGE_MANIFEST_UPDATED',flush=True)


def batch(audio_root: Path, image_root: Path, output: Path, grid):
    used = set()
    for audio in sorted(audio_root.rglob('*')):
        if not audio.is_file() or audio.suffix.lower() not in ('.mp3','.flac','.wav'):
            continue
        relative = audio.relative_to(audio_root).with_suffix('')
        key = relative.as_posix().casefold()
        if key in used:
            raise ValueError(f'conflicting_audio_basename: {relative}')
        used.add(key)
        parent = image_root / relative.parent
        matches = [p for p in parent.iterdir() if p.stem.casefold()==relative.name.casefold() and p.suffix.lower() in ('.jpg','.jpeg','.png')] if parent.exists() else []
        if len(matches) > 1:
            raise ValueError(f'conflicting_cover: {relative}')
        if not matches:
            print(f'MISSING={relative}')
            continue
        image, data, chars = cover_ascii(matches[0], *grid)
        destination = output / relative.parent / (relative.name+'.cover.adv')
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        image.save(destination.with_suffix('.preview.png'))
        image.resize((image.width*4,image.height*4),Image.Resampling.NEAREST).save(destination.with_suffix('.preview-4x.png'))
        print(f'COVER={destination}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--fonts', action='store_true')
    parser.add_argument('--manifest-only', action='store_true')
    parser.add_argument('--audio-root', type=Path)
    parser.add_argument('--image-root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--grid', choices=['26x20','30x24','34x26','40x32','48x40'], default='40x32')
    args = parser.parse_args()
    if args.manifest_only:
        refresh_manifest()
    elif args.audio_root:
        if not args.image_root or not args.output:
            parser.error('batch requires --image-root and --output')
        batch(args.audio_root.resolve(), args.image_root.resolve(), args.output.resolve(), tuple(map(int,args.grid.split('x'))))
    else:
        build(args.download, args.fonts)
