"""Execute C++ constexpr timeout tests with the existing ESP32 compiler.

No object/executable is written and no new host compiler is installed.
This proves timer arithmetic/contracts, not ADV media or audio performance.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_COMPILER = Path(r'B:\PlatformIO\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe')


def check(compiler):
    command = [str(compiler), '-std=gnu++11', '-fsyntax-only', '-Isrc',
               'test/p3abc/phase_timing.cpp']
    good = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if good.returncode:
        raise SystemExit(good.stdout+good.stderr)
    old = subprocess.run(command+['-DADV_GATE_REPRODUCE_OLD_TIMER=1'],
                         cwd=ROOT, capture_output=True, text=True)
    if old.returncode == 0 or 'phase transition underflow regression' not in old.stderr:
        raise SystemExit('Regression test did not catch the old timer defect')
    source = (ROOT/'src/player/p3abc/P3ABCGate.cpp').read_text(encoding='utf-8-sig')
    service = source.split('void P3ABCGate::service(')[1].split('void P3ABCGate::closeQuotaFiles')[0]
    assert service.index('const GatePhaseStamp entered') < service.index('switch(phase_)')
    assert service.index('const uint32_t checkedAt=millis();') > service.index('case Phase::RebootCheck:')
    assert 'gatePhaseTimedOut(entered,current,checkedAt,eligible)' in service
    assert 'now-phaseAt_>45000' not in service
    for terminal in ('Disabled', 'Passed', 'Failed'):
        assert f'phase_!=Phase::{terminal}' in service
    print('C++ TIMER: 13 compile-time assertions PASS; old implementation rejected as expected')
    print('GATE INTEGRATION: fresh clock / phase identity / terminal guards PASS')
    print('No ADV media/audio validation claimed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=DEFAULT_COMPILER)
    check(parser.parse_args().compiler.resolve(strict=True))
