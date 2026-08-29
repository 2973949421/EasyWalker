"""Prepare and safely sync the KINO and 熱・情 private libraries.

Audio is fetched only after an explicit user request through GD音乐台
(music.gdstudio.xyz), for private non-commercial use. Metadata and lyrics are
matched to the same NetEase track IDs. Generated media stays under the ignored
test-data/local tree; this script never deletes SD content or changes state.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import zlib
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageOps

from font_index_v2 import generate as generate_idx2
from prepare_p3_media import LOCAL, PACKAGE, cover_ascii, font_sources, make_font, sha, validate_cover
from prepare_p3_refine import records, source as font_source

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")

ROOT = Path(__file__).resolve().parents[1]
PRIVATE = LOCAL / "expanded-libraries"
RAW = PRIVATE / "raw"
FFDIR = Path("B:/Tools/FFmpeg/ffmpeg-9.0.1-essentials_build/bin")
CANTO_SOURCE = Path("B:/sharewithlight/SONG/粤语迷幻/ADV导入包")
UA = "EasyWalker-private-import/1.0"
LIBRARY_SIZE = (135, 154)
KINO_LIBRARY_SIZE = (135, 173)
KINO_PORTRAIT = RAW / "Victor_Tsoi_1986.jpg"
KINO_PORTRAIT_SOURCE = "https://commons.wikimedia.org/wiki/File:Victor_Tsoi_1986.jpg"
FONT_FACES = ("cjk-12", "cjk-14", "cjk-16", "cjk-18", "library-cjk-12", "library-cjk-18")
LYRIC_LINE = re.compile(r"^(\[[0-9]{1,3}:[0-9]{2}(?:[.:][0-9]{1,3})?\])(.*)$")
DROP_CREDITS = ("作词", "作曲", "编曲", "制作", "混音", "录音", "母带", "吉他", "贝斯", "鼓", "和声")


@dataclass(frozen=True)
class Track:
    library: str
    order: str
    track_id: int
    title: str
    artist: str
    album: str
    duration_ms: int
    cover_url: str
    translated: bool = False

    @property
    def basename(self) -> str:
        safe = self.title.translate(str.maketrans({'/': '・', '\\': '・', ':': '：', '*': '＊', '?': '？', '"': '＂', '<': '＜', '>': '＞', '|': '｜'}))
        return f"{self.order} - {safe}"


PASSION_COVER = "https://p1.music.126.net/6iR2H50FV5Eetj_xioWS8Q==/109951173832576484.jpg"
PASSION_DATA = [
    ("1-01",186981,"Overture",36333),("1-02",186987,"夢死醉生",242293),
    ("1-03",187001,"寂寞有害",280773),("1-04",187005,"不要愛他",214933),
    ("1-05",187009,"愛慕",278493),("1-06",187014,"風繼續吹",320040),
    ("1-07",187017,"儂本多情",243933),("1-08",187020,"側面 / 放蕩",271226),
    ("1-09",187023,"妳在何地",258973),("1-10",187026,"American Pie",279266),
    ("1-11",187030,"春夏秋冬",299466),("1-12",187034,"沒有愛",250066),
    ("1-13",187038,"路過蜻蜓",234066),("1-14",187042,"無心睡眠",186133),
    ("1-15",187048,"我的心裡沒有他",143866),("1-16",187052,"熱情的沙漠",182600),
    ("1-17",187055,"大熱",214800),("2-01",187059,"紅",384466),
    ("2-02",187063,"枕頭",294426),("2-03",187066,"左右手",322173),
    ("2-04",187067,"我 (國)",284000),("2-05",187070,"陪你倒數",277293),
    ("2-06",187073,"H₂O Medley: H₂O / 少女心事 / 第一次 / 不羈的風",437733),
    ("2-07",187076,"Monica",124933),("2-08",187080,"Stand Up / Twist & Shout / Stand Up",264333),
    ("2-09",187083,"為妳鍾情",234533),("2-10",187088,"I Honestly Love You",240506),
    ("2-11",187092,"至少還有你",340493),("2-12",187096,"共同渡過",309800),
]

KINO_DATA = [
    ("01",26668133,"Группа крови","Группа крови",284002,"https://p1.music.126.net/V_gxdxF2QPS1dah8xZfryg==/109951170416021679.jpg"),
    ("02",26710584,"Хочу перемен","Последний герой",295000,"https://p1.music.126.net/jrEDyug7ubULYf3UrXzHjg==/109951170416026069.jpg"),
    ("03",1634119,"Кукушка","Чёрный Альбом",400306,"https://p1.music.126.net/8nkJkyEA4LrMSdkye4CQzA==/109951170118022287.jpg"),
    ("04",1634184,"Пачка сигарет","Звезда по имени Солнце",268000,"https://p1.music.126.net/01e2EXbOGQePYDvKCy4Kig==/109951170197500877.jpg"),
    ("05",1634162,"Звезда по имени Солнце","Звезда по имени Солнце",226133,"https://p1.music.126.net/01e2EXbOGQePYDvKCy4Kig==/109951170197500877.jpg"),
    ("06",26668134,"Закрой за мной дверь, я ухожу","Группа крови",256096,"https://p2.music.126.net/V_gxdxF2QPS1dah8xZfryg==/109951170416021679.jpg"),
    ("07",26710589,"Последний герой","Последний герой",185000,"https://p1.music.126.net/jrEDyug7ubULYf3UrXzHjg==/109951170416026069.jpg"),
    ("08",26710592,"В наших глазах","Последний герой",225436,"https://p1.music.126.net/jrEDyug7ubULYf3UrXzHjg==/109951170416026069.jpg"),
]

TRACKS = [Track("熱・情",o,i,t,"張國榮","熱・情",d,PASSION_COVER) for o,i,t,d in PASSION_DATA]
TRACKS += [Track("KINO",o,i,t,"Кино",a,d,c,True) for o,i,t,a,d,c in KINO_DATA]


def request_bytes(url: str, limit: int, attempts: int = 4) -> bytes:
    last = None
    for attempt in range(attempts):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA, "Referer": "https://music.163.com/"})
            with urllib.request.urlopen(req, timeout=60) as response:
                data = response.read(limit + 1)
            if len(data) > limit:
                raise ValueError(f"download_limit:{url}")
            return data
        except Exception as exc:  # transient CDN / 503 failures are expected
            last = exc
            if attempt + 1 < attempts:
                time.sleep(15 * (attempt + 1))
    raise RuntimeError(f"request_failed:{url}:{last}")


def request_json(url: str) -> dict:
    return json.loads(request_bytes(url, 2 * 1024 * 1024).decode("utf-8-sig"))


def ffprobe(path: Path) -> dict:
    result = subprocess.run([str(FFDIR / "ffprobe.exe"), "-v", "error", "-show_format", "-show_streams", "-of", "json", str(path)], capture_output=True, check=True)
    return json.loads(result.stdout)


def audio_packet_hash(path: Path) -> str:
    result = subprocess.run([str(FFDIR / "ffmpeg.exe"), "-v", "error", "-i", str(path), "-map", "0:a:0", "-c", "copy", "-f", "hash", "-hash", "sha256", "-"], capture_output=True, check=True, text=True)
    return result.stdout.strip().split("=", 1)[-1]


def gd_audio(track: Track, raw: Path) -> None:
    if raw.is_file() and raw.stat().st_size > 128 * 1024:
        return
    endpoint = "https://music-api.gdstudio.xyz/api.php?" + urllib.parse.urlencode({"types":"url","source":"netease","id":track.track_id,"br":320})
    answer = request_json(endpoint)
    if not answer.get("url") or int(answer.get("br", 0)) < 320:
        raise ValueError(f"gd_audio_unavailable:{track.track_id}:{answer}")
    raw.parent.mkdir(parents=True, exist_ok=True)
    raw.write_bytes(request_bytes(answer["url"], 40 * 1024 * 1024))
    # Keep the documented service below 50 API calls per five minutes.
    time.sleep(7)


def prepare_audio(track: Track) -> dict:
    raw = RAW / track.library / f"{track.track_id}.mp3"
    gd_audio(track, raw)
    before = ffprobe(raw)
    duration = round(float(before["format"]["duration"]) * 1000)
    if abs(duration - track.duration_ms) > 2200:
        raise ValueError(f"duration_mismatch:{track.track_id}:{duration}:{track.duration_ms}")
    stream = next(s for s in before["streams"] if s.get("codec_type") == "audio")
    target = PACKAGE / "Music" / track.library / f"{track.basename}.mp3"
    target.parent.mkdir(parents=True, exist_ok=True)
    copy_audio = stream.get("codec_name") == "mp3" and stream.get("sample_rate") == "44100" and stream.get("channels") == 2 and int(stream.get("bit_rate", 0)) >= 300000
    command = [str(FFDIR / "ffmpeg.exe"), "-y", "-v", "error", "-i", str(raw), "-map", "0:a:0", "-map_metadata", "-1", "-vn"]
    command += ["-c:a", "copy"] if copy_audio else ["-c:a", "libmp3lame", "-b:a", "320k", "-ar", "44100", "-ac", "2"]
    command += ["-id3v2_version", "3", "-metadata", f"title={track.title}", "-metadata", f"artist={track.artist}", "-metadata", f"album={track.album}", "-metadata", f"track={track.order}", str(target)]
    subprocess.run(command, check=True)
    after = ffprobe(target)
    astream = next(s for s in after["streams"] if s.get("codec_type") == "audio")
    if astream.get("sample_rate") != "44100" or astream.get("channels") != 2:
        raise ValueError(f"output_audio_format:{target}")
    tags = {k.lower():v for k,v in after["format"].get("tags",{}).items()}
    if tags.get("title") != track.title or tags.get("artist") != track.artist or tags.get("album") != track.album:
        raise ValueError(f"output_tags:{target}:{tags}")
    return {"id":track.track_id,"file":target.relative_to(PACKAGE).as_posix(),"duration_ms":duration,"sha256":sha(target),"audio_packet_sha256":audio_packet_hash(target),"copied_audio":copy_audio}


def clean_lrc(raw: str, drop_credits: bool) -> str:
    output = []
    for line in raw.replace("\r", "").split("\n"):
        match = LYRIC_LINE.match(line.strip())
        if not match:
            continue
        text = match.group(2).strip()
        if not text or (drop_credits and any(text.startswith(prefix) for prefix in DROP_CREDITS)):
            continue
        output.append(match.group(1) + text)
    return "\n".join(output) + ("\n" if output else "")


def prepare_lyrics(track: Track) -> dict:
    endpoint = f"https://music.163.com/api/song/lyric?id={track.track_id}&lv=-1&tv=-1"
    answer = request_json(endpoint)
    original = clean_lrc(answer.get("lrc",{}).get("lyric", ""), track.library == "熱・情")
    translated = clean_lrc(answer.get("tlyric",{}).get("lyric", ""), False) if track.translated else ""
    directory = PACKAGE / "Lyrics" / track.library
    directory.mkdir(parents=True, exist_ok=True)
    base = directory / f"{track.basename}.lrc"
    if original:
        base.write_text(original, encoding="utf-8")
    elif base.exists():
        base.unlink()
    translated_path = directory / f"{track.basename}.zh-Hans.lrc"
    if translated:
        translated_path.write_text(translated, encoding="utf-8")
    elif translated_path.exists():
        translated_path.unlink()
    return {"original_cues":len(original.splitlines()),"translated_cues":len(translated.splitlines())}


def download_art(url: str) -> Path:
    key = hashlib.sha256(url.encode()).hexdigest()[:16]
    target = PRIVATE / "art" / f"{key}.jpg"
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(request_bytes(url, 12 * 1024 * 1024))
        with Image.open(target) as image:
            image.verify()
    return target


def library_cover(image: Image.Image, size: tuple[int, int] = LIBRARY_SIZE,
                  centering: tuple[float, float] = (.5, .5)) -> bytes:
    fitted = ImageOps.fit(ImageOps.exif_transpose(image).convert("RGB"), size, Image.Resampling.LANCZOS, centering=centering)
    pixels = b"".join(struct.pack("<H",((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b in fitted.getdata())
    return struct.pack("<4s6H2I",b"LCOV",1,24,fitted.width,fitted.height,1,0,len(pixels),zlib.crc32(pixels))+pixels


def write_covers() -> list[dict]:
    art = {url:download_art(url) for url in sorted({t.cover_url for t in TRACKS})}
    report = []
    for track in TRACKS:
        preview, data, _ = cover_ascii(art[track.cover_url], 40, 32, (135,135))
        if validate_cover(data) != (135,135):
            raise ValueError("song_cover_dimensions")
        target = PACKAGE / "ADVWalkman" / "covers" / track.library / f"{track.basename}.cover.adv"
        target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
        source = PACKAGE / "CoverSource" / track.library / f"{track.basename}.jpg"
        source.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(art[track.cover_url],source)
        report.append({"file":target.relative_to(PACKAGE).as_posix(),"dimensions":[135,135],"sha256":sha(target)})
    # 熱・情 uses the official album art. KINO deliberately uses one public-
    # domain Victor Tsoi portrait; a four-album collage is not the collection's
    # visual identity and is difficult to read on the 135px display.
    with Image.open(art[PASSION_COVER]) as image:
        data = library_cover(image)
    target = PACKAGE / "ADVWalkman/library-covers/folders/熱・情/cover.adv"
    target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
    write_kino_portrait_cover()
    return report


def write_kino_portrait_cover() -> Path:
    if not KINO_PORTRAIT.is_file():
        raise ValueError(f"kino_portrait_missing:{KINO_PORTRAIT}:{KINO_PORTRAIT_SOURCE}")
    with Image.open(KINO_PORTRAIT) as image:
        portrait = ImageOps.exif_transpose(image).convert("RGB")
        fitted = ImageOps.fit(portrait, KINO_LIBRARY_SIZE, Image.Resampling.LANCZOS, centering=(.5, .38))
        data = library_cover(portrait, KINO_LIBRARY_SIZE, (.5, .38))
    target = PACKAGE / "ADVWalkman/library-covers/folders/KINO/cover.adv"
    target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
    source = PACKAGE / "CoverSource/KINO/Victor_Tsoi_1986.jpg"
    source.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(KINO_PORTRAIT, source)
    preview = Image.new("RGB", (135, 240), (8, 12, 8));preview.paste(fitted, (0, 0))
    draw = ImageDraw.Draw(preview);draw.text((52, 176), "KINO", fill="white")
    for x, y, selected in ((29, 232, False), (67, 222, True), (105, 232, False)):
        draw.ellipse((x-26, y-26, x+26, y+26), fill="black", outline=(80, 80, 80), width=2)
        draw.ellipse((x-9, y-9, x+9, y+9), fill=(248, 190, 0) if selected else (70, 60, 40))
    preview_path = PRIVATE / "kino-library-135x240.png";preview_path.parent.mkdir(parents=True, exist_ok=True);preview.save(preview_path)
    if validate_library_cover(target) != KINO_LIBRARY_SIZE:raise ValueError("kino_library_cover_dimensions")
    return target


def merge_and_fix_existing() -> list[str]:
    if not (CANTO_SOURCE / "Music/粤语迷幻").is_dir():
        raise ValueError(f"cantopop_source_missing:{CANTO_SOURCE}")
    for relative in ("Music/粤语迷幻","Lyrics/粤语迷幻","ADVWalkman/covers/粤语迷幻","ADVWalkman/library-covers/folders/粤语迷幻","CoverSource/粤语迷幻"):
        shutil.copytree(CANTO_SOURCE / relative, PACKAGE / relative, dirs_exist_ok=True)
    lyric = PACKAGE / "Lyrics/粤语迷幻/禁色.lrc"
    lines = [line for line in lyric.read_text(encoding="utf-8-sig").splitlines() if not line.lower().startswith("[offset:")]
    lyric.write_text("[offset:+1500]\n" + "\n".join(lines) + "\n", encoding="utf-8")
    # The original 約會 jacket has a very large intentional black frame. Use
    # its inner gold-framed composition for the 135x135 player canvas.
    source = CANTO_SOURCE / "CoverSource/粤语迷幻/約會.jpg"
    with Image.open(source) as image:
        rgb = ImageOps.exif_transpose(image).convert("RGB")
        w,h = rgb.size
        crop = rgb.crop((round(w*.18),round(h*.17),round(w*.96),round(h*.95)))
        preview,data,_ = cover_ascii_from_image(crop)
    target = PACKAGE / "ADVWalkman/covers/粤语迷幻/約會.cover.adv"
    target.write_bytes(data)
    preview.save(PACKAGE / "CoverSource/粤语迷幻/約會-device-crop.jpg")
    return [lyric.relative_to(PACKAGE).as_posix(), target.relative_to(PACKAGE).as_posix()]


def cover_ascii_from_image(image: Image.Image):
    temporary = PRIVATE / "art/appointment-device-crop.png"
    temporary.parent.mkdir(parents=True, exist_ok=True); image.save(temporary)
    return cover_ascii(temporary,40,32,(135,135))


def extend_fonts() -> list[str]:
    required = set()
    for track in TRACKS:
        required.update(ord(c) for c in track.title + track.artist + track.album if ord(c)>=256)
    for lyric in (PACKAGE/"Lyrics").rglob("*.lrc"):
        required.update(ord(c) for c in lyric.read_text(encoding="utf-8-sig") if ord(c)>=256)
    destination = PACKAGE / "ADVWalkman/fonts"
    changed=[]
    for name in FONT_FACES:
        known=set(records(destination/name)); wanted=known|required
        if wanted==known:
            continue
        size=int(name.rsplit("-",1)[1])
        if name.startswith("library-"):
            sources=[font_source("C:/Windows/Fonts/times.ttf"),font_source("C:/Windows/Fonts/STXINGKA.TTF"),*font_sources(False)]
        else:
            sources=font_sources(False)
        report=make_font(name,size,wanted,sources,destination,embolden=name.startswith("library-"))
        if report["missing"]:
            raise ValueError(f"font_missing:{name}:{report['missing']}")
        generate_idx2((destination/name).with_suffix(".idx"))
        changed.append(name)
    return changed


def build() -> None:
    PRIVATE.mkdir(parents=True,exist_ok=True)
    merged = merge_and_fix_existing()
    report=[]
    for index,track in enumerate(TRACKS,1):
        item=prepare_audio(track);item.update(prepare_lyrics(track));report.append(item)
        print(f"PREPARED {index}/{len(TRACKS)} {track.library} {track.title}",flush=True)
    covers=write_covers()
    # Recompile the established AveMujica artwork to the same 135x154 contract.
    subprocess.run([sys.executable,str(Path(__file__).with_name("prepare_p3d.py"))],check=True)
    fonts=extend_fonts()
    manifest={"schema":1,"source":"GD音乐台(music.gdstudio.xyz) + NetEase metadata/lyrics","private_noncommercial":True,
              "library_cover_dimensions":list(LIBRARY_SIZE),"song_cover_dimensions":[135,135],"tracks":report,
              "covers":covers,"existing_fixes":merged,"font_faces":list(FONT_FACES),"fonts_changed":fonts}
    (PRIVATE/"manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(f"EXPANDED_LIBRARIES_PASS tracks={len(report)} fonts={','.join(fonts)}",flush=True)


def validate_library_cover(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if len(data) < 24:
        raise ValueError(f"library_cover_short:{path}")
    magic, version, header_size, width, height, pixel_format, flags, payload_size, expected_crc = struct.unpack(
        "<4s6H2I", data[:24]
    )
    payload = data[header_size:]
    if magic != b"LCOV" or version != 1 or header_size != 24 or pixel_format != 1 or flags != 0:
        raise ValueError(f"library_cover_header:{path}")
    if payload_size != width * height * 2 or len(payload) != payload_size:
        raise ValueError(f"library_cover_size:{path}")
    if zlib.crc32(payload) != expected_crc:
        raise ValueError(f"library_cover_crc:{path}")
    return width, height


def verify() -> None:
    manifest_path = PRIVATE / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if len(manifest.get("tracks", [])) != len(TRACKS):
        raise ValueError("manifest_track_count")
    manifest_by_id = {int(item["id"]): item for item in manifest["tracks"]}
    for track in TRACKS:
        item = manifest_by_id.get(track.track_id)
        if item is None:
            raise ValueError(f"manifest_track_missing:{track.track_id}")
        audio = PACKAGE / item["file"]
        if not audio.is_file() or sha(audio) != item["sha256"]:
            raise ValueError(f"audio_hash:{audio}")
        probe = ffprobe(audio)
        stream = next(value for value in probe["streams"] if value.get("codec_type") == "audio")
        tags = {key.lower(): value for key, value in probe["format"].get("tags", {}).items()}
        duration_ms = round(float(probe["format"]["duration"]) * 1000)
        if stream.get("sample_rate") != "44100" or stream.get("channels") != 2:
            raise ValueError(f"audio_format:{audio}")
        if tags.get("title") != track.title or tags.get("artist") != track.artist or tags.get("album") != track.album:
            raise ValueError(f"audio_tags:{audio}")
        if abs(duration_ms - track.duration_ms) > 2200:
            raise ValueError(f"audio_duration:{audio}:{duration_ms}:{track.duration_ms}")
        cover = PACKAGE / "ADVWalkman/covers" / track.library / f"{track.basename}.cover.adv"
        if validate_cover(cover.read_bytes()) != (135, 135):
            raise ValueError(f"song_cover_dimensions:{cover}")
        lyric = PACKAGE / "Lyrics" / track.library / f"{track.basename}.lrc"
        if item["original_cues"] and len(lyric.read_text(encoding="utf-8-sig").splitlines()) != item["original_cues"]:
            raise ValueError(f"lyric_cues:{lyric}")
        translation = PACKAGE / "Lyrics" / track.library / f"{track.basename}.zh-Hans.lrc"
        if item["translated_cues"] and len(translation.read_text(encoding="utf-8-sig").splitlines()) != item["translated_cues"]:
            raise ValueError(f"translated_cues:{translation}")
    for library in ("粤语迷幻", "AveMujica", "熱・情"):
        cover = PACKAGE / "ADVWalkman/library-covers/folders" / library / "cover.adv"
        if validate_library_cover(cover) != LIBRARY_SIZE:
            raise ValueError(f"library_cover_dimensions:{cover}")
    kino_cover = PACKAGE / "ADVWalkman/library-covers/folders/KINO/cover.adv"
    if validate_library_cover(kino_cover) != KINO_LIBRARY_SIZE:
        raise ValueError(f"library_cover_dimensions:{kino_cover}")
    canto_lyric = PACKAGE / "Lyrics/粤语迷幻/禁色.lrc"
    if canto_lyric.read_text(encoding="utf-8-sig").splitlines()[0] != "[offset:+1500]":
        raise ValueError("forbidden_colour_offset")
    required = {ord(character) for track in TRACKS for character in track.title + track.artist + track.album if ord(character) >= 256}
    for face in FONT_FACES:
        base = PACKAGE / "ADVWalkman/fonts" / face
        if not all(base.with_suffix(suffix).is_file() for suffix in (".vlw", ".idx", ".idx2")):
            raise ValueError(f"font_files:{face}")
        missing = required - set(records(base))
        if missing:
            raise ValueError(f"font_coverage:{face}:{sorted(missing)[:8]}")
    print(
        f"EXPANDED_LIBRARIES_VERIFY_PASS tracks={len(TRACKS)} "
        f"kino={len(KINO_DATA)} passion={len(PASSION_DATA)} "
        f"song_covers={len(TRACKS)} library_covers=4",
        flush=True,
    )


def copy_verified(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    if sha(source)!=sha(target):raise ValueError(f"copy_verify:{target}")


def sync(sd_text: str) -> None:
    sd=Path(sd_text).resolve()
    if not (sd/"Music/AveMujica").is_dir() or not (sd/"ADVWalkman").is_dir():
        raise ValueError(f"unexpected_sd:{sd}")
    manifest=json.loads((PRIVATE/"manifest.json").read_text(encoding="utf-8"))
    files=[]
    for library in ("KINO","熱・情"):
        files.extend((PACKAGE/"Music"/library).glob("*.mp3"));files.extend((PACKAGE/"Lyrics"/library).glob("*.lrc"))
        files.extend((PACKAGE/"ADVWalkman/covers"/library).glob("*.cover.adv"))
        files.append(PACKAGE/f"ADVWalkman/library-covers/folders/{library}/cover.adv")
    files += [PACKAGE/"Lyrics/粤语迷幻/禁色.lrc",PACKAGE/"ADVWalkman/covers/粤语迷幻/約會.cover.adv",
              PACKAGE/"ADVWalkman/library-covers/folders/AveMujica/cover.adv"]
    for face in manifest.get("font_faces", manifest.get("fonts", [])):
        files.extend((PACKAGE/"ADVWalkman/fonts").glob(face+".*"))
    for source in files:
        if not source.is_file():raise ValueError(f"missing_package_file:{source}")
        copy_verified(source,sd/source.relative_to(PACKAGE))
    print(f"SD_SYNC_PASS files={len(files)} tracks={len(manifest['tracks'])} root={sd}",flush=True)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--build",action="store_true");parser.add_argument("--verify",action="store_true");parser.add_argument("--refresh-kino-cover",action="store_true");parser.add_argument("--sync")
    args=parser.parse_args()
    if args.build:build()
    if args.refresh_kino_cover:
        target=write_kino_portrait_cover();print(f"KINO_PORTRAIT_PASS file={target} dimensions={KINO_LIBRARY_SIZE}",flush=True)
    if args.verify:verify()
    if args.sync:sync(args.sync)
