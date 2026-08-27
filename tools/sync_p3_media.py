"""Copy this prepared package only. Explicit --sd-root required; no deletion.

Run only after the user confirms the SD is in the PC. No serial/Flash calls.
"""
import argparse
import hashlib
import shutil
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
PACKAGE=ROOT/'test-data/local/p3-media/package'
BENCH_HASH='4003b057b19ca95bae78e66b3536557e342d1105315c2a6217f4475c0db51d63'

def sha(path):
    digest=hashlib.sha256()
    with path.open('rb') as file:
        for data in iter(lambda:file.read(1024*1024),b''):digest.update(data)
    return digest.hexdigest()

def sync(root):
    root=root.resolve(strict=True)
    if not (root/'Music').is_dir():raise ValueError('Expected an existing SD /Music directory')
    benchmark=root/'Music/ADVWalkmanBenchmark/benchmark.mp3'
    if benchmark.stat().st_size!=11972484 or sha(benchmark)!=BENCH_HASH:raise ValueError('Benchmark mismatch; left unchanged')
    manifest=ROOT/'test-data/local/p3-media/PACKAGE.sha256'
    expected={line.split('  ',1)[1]:line.split('  ',1)[0] for line in manifest.read_text(encoding='utf-8').splitlines()}
    previous_path=root/'ADVWalkman/p3-media-package.sha256'
    previous={}
    if previous_path.exists():
        previous={line.split('  ',1)[1]:line.split('  ',1)[0] for line in previous_path.read_text(encoding='utf-8').splitlines()}
    copies=[]
    for relative,digest in expected.items():
        src=(PACKAGE/relative).resolve(strict=True);dst=(root/relative).resolve()
        if not src.is_relative_to(PACKAGE.resolve()) or not dst.is_relative_to(root):raise ValueError('Unsafe relative path')
        if sha(src)!=digest:raise ValueError(f'Package changed: {relative}')
        if dst.exists():
            existing=sha(dst)
            if existing==digest:continue
            if previous.get(relative)!=existing:raise ValueError(f'Unowned/changed SD file; stopped: {relative}')
        copies.append((src,dst,digest))
    artifact=ROOT/'artifacts/ADV-Walkman-P3ABC-Gate.bin'
    if not artifact.is_file() or not 0<artifact.stat().st_size<=0x140000:raise ValueError('Build joint Gate first')
    for src,dst,digest in copies:
        dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
        if sha(dst)!=digest:raise IOError(f'Copy verification: {dst}')
        print(f'COPIED={dst}')
    destination=root/'firmware'/artifact.name
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(artifact,destination)
    if sha(destination)!=sha(artifact):raise IOError('Firmware copy verification')
    previous_path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(manifest,previous_path)
    print(f'FIRMWARE={destination}\nSHA256={sha(destination)}\nNo other files removed or modified.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--sd-root',type=Path,required=True)
    sync(parser.parse_args().sd_root)
