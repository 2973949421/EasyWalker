"""Record existing successful build outputs; no compilation or SD mutation."""
import argparse,hashlib,json,shutil,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[1]
TARGETS={'player-dev':'ADV-Walkman-Dev.bin','player-p3abc-gate':'ADV-Walkman-P3ABC-Gate.bin','player-p3a-gate':'ADV-Walkman-P3A-Gate.bin','player-p1-gate-a':'ADV-Walkman-P1-Gate-A.bin','player-p1-gate-b':'ADV-Walkman-P1-Gate-B.bin','player-p2-gate':'ADV-Walkman-P2-Gate.bin'}
def record(version='0.8.2',targets=None):
    report=[];artifacts=ROOT/'artifacts';artifacts.mkdir(exist_ok=True)
    for target,name in TARGETS.items():
        if targets and target not in targets:continue
        directory=ROOT/'.pio/build'/target;binary=directory/'firmware.bin';data=binary.read_bytes()
        assert len(data)<=0x140000
        with (directory/'firmware.elf').open('rb') as stream:
            elf=ELFFile(stream);ram=sum(s['sh_size'] for s in elf.iter_sections() if s.name in ('.dram0.data','.dram0.bss'))
            memory=None
            for symbol in elf.get_section_by_name('.symtab').iter_symbols():
                if 'p3MemoryReport' in symbol.name:
                    section=elf.get_section(symbol['st_shndx']);offset=symbol['st_value']-section['sh_addr']
                    values=struct.unpack_from('<6I',section.data(),offset)
                    memory=dict(zip(('media_bytes','media_plus_events_bytes','event_queue_bytes','existing_shared_row_bytes','glyph_work_bytes','lyrics_work_bytes'),values))
                    assert values[1]<=49152
        sha=hashlib.sha256(data).hexdigest();shutil.copy2(binary,artifacts/name)
        item=dict(environment=target,artifact=name,bytes=len(data),sha256=sha,static_ram_bytes=ram)
        if memory:item['memory']=memory
        report.append(item)
    output=artifacts/f'p3-{version}-builds.json';output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(output);print(json.dumps(report,indent=2))
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--version',default='0.8.2');parser.add_argument('--targets',nargs='+',choices=TARGETS)
    args=parser.parse_args();record(args.version,args.targets)
