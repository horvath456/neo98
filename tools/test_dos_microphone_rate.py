#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""changing the DSP rate must not extrapolate microphone PCM. Use serial.py."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98

SOURCE = r'''
#include <mutex>
#include <atomic>
#include <cstdio>
namespace Sound {
constexpr unsigned InputRate = 44100;
struct Format { unsigned rate, channels, bits; };
struct Microphone {
 static unsigned Open(){return 0;}
 static void Start(unsigned,unsigned long long){}
 static void Close(unsigned){}
 static void Advance(unsigned long long){}
 static unsigned Read(unsigned,unsigned char*,unsigned,const Format&){return 0;}
};
}
struct Machine {
 bool state_loading=false;
#include NEO98_MICROPHONE_SOURCE
};
int main(){
 Machine m;
 m.microphone_count=32;
 m.microphone_ring[0]=0;
 m.microphone_ring[1]=-32768;
 m.microphone_ring[2]=32767;
 unsigned char sample=0;
 // Both rates can be selected with the legacy DSP time-constant command.
 Machine::MicrophoneRead(&m,&sample,1,33333);
 if(sample!=128)return 1;
 // At 33,333 Hz the first call leaves a nonzero interpolation remainder.
 // Starting a 4,000 Hz capture must begin at the next PCM sample, not
 // extrapolate across the full-scale rising edge with the old remainder.
 Machine::MicrophoneRead(&m,&sample,1,4000);
 if(sample!=0){std::printf("rate change returned %u, expected 0\n",sample);return 2;}
 // Direct ADC still reports the most recent input independent of DSP rate.
 m.microphone_last=32767;
 Machine::MicrophoneRead(&m,&sample,1,0);
 if(sample!=255)return 3;
 m.CloseMicrophone();
 Machine::MicrophoneRead(&m,&sample,1,8000);
 if(sample!=128)return 4;
 std::puts("MICROPHONE RATE PASS: 33333 -> 4000 Hz, direct ADC, closed-input silence");
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'tools/host_dos_microphone.inc')
    args=parser.parse_args()
    out=ROOT/'build/dos-microphone-rate'
    out.mkdir(parents=True,exist_ok=True)
    source=out/'test.cpp'
    source.write_text(SOURCE)
    executable=out/'test'
    subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                    '-DNEO98_MICROPHONE_SOURCE='+json.dumps(str(args.source.resolve())),
                    str(source),'-o',str(executable)],env=neo98.tool_environment(),check=True)
    return subprocess.run([str(executable)],timeout=20).returncode


if __name__=='__main__':
    raise SystemExit(main())
