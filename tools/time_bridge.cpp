// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_time.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using U = unsigned;
using Q = unsigned long long;
extern "C" void time_family_run(const U *a, U *out)
{
    for (U i = 0; i < 32; ++i) out[i] = 0xcccccccc;
    out[0] = 0; out[1] = a[4];
    const Win98Time::Bias bias{Q(a[2]) | (Q(a[3]) << 32), a[1]};
    const Q input = Q(a[5]) | (Q(a[6]) << 32);
    Q file = 0xccccccccccccccccull;
    U date = 0xcccc, time = 0xcccc, fraction = 0xcc;
    auto put = [&](Q value) { out[2] = U(value); out[3] = U(value >> 32); };
    switch (a[0])
    {
        case 0:
        {
            Win98Time::SystemTime in{};
            unsigned short words[8];
            for (U i = 0; i < 8; ++i) words[i] = a[5+i];
            std::memcpy(&in, words, sizeof words);
            out[0] = Win98Time::SystemToFile(in, file); put(file); break;
        }
        case 1:
        {
            Win98Time::SystemTime fields{};
            Win98Time::FileToSystem(input, fields); out[0] = 1;
            unsigned short words[8];
            std::memcpy(words, &fields, sizeof words);
            for (U i = 0; i < 8; ++i) out[2+i] = words[i];
            break;
        }
        case 2:out[0] = Win98Time::Compare(input, Q(a[7]) | (Q(a[8]) << 32));break;
        case 3:out[0] = 1;put(Win98Time::ToLocal(input,bias.ticks));break;
        case 4:out[0] = 1;put(Win98Time::ToUtc(input,bias.ticks));break;
        case 5:
        {
            const U error = Win98Time::DosToFile(a[5],a[6],bias,file);
            out[0] = !error; if(error)out[1]=error;put(file);break;
        }
        case 6:
        {
            const U error = Win98Time::FileToDos(input,bias,date,time);
            out[0] = !error;if(error)out[1]=error;out[2]=date;out[3]=time;break;
        }
        case 7:
            date=a[6]&65535;time=a[7]&65535;fraction=a[8]&255;file=Q(a[9])|(Q(a[10])<<32);
            out[0]=Win98Time::DosService(a[5],date,time,fraction,file,bias.minutes);
            out[2]=date;out[3]=time;out[4]=fraction;out[5]=U(file);out[6]=U(file>>32);break;
        case 8:out[0]=Win98Time::Years(a[5]);break;
        case 9:Win98Time::Split(input,date,time);out[2]=date;out[3]=time;break;
        case 10:put(Win98Time::Compose(a[5],a[6]));break;
        case 11:put(Win98Time::MagicDivide(input,Q(a[7])|(Q(a[8])<<32),a[9]));break;
        case 12:out[0]=Win98Time::UnixToDos(a[5],bias.minutes);break;
        case 13:out[0]=Win98Time::DosToUnix(a[5],bias.minutes);break;
        case 14:out[0]=Win98Time::DosRange(input,bias.minutes) ? ~0u : 0;break;
        case 15:out[0]=Win98Time::FileToUnix(input,fraction);out[2]=fraction;break;
        case 16:put(Win98Time::UnixToFile(a[5]));break;
        default:assert(false);
    }
}
#ifdef TIME_FAMILY_STANDALONE
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=std::fopen(argv[1],"rb");assert(f);U count;
    assert(std::fread(&count,4,1,f)==1);
    for(U i=0;i<count;++i)
    {
        U input[16],expected[32],out[32];
        assert(std::fread(input,4,16,f)==16 && std::fread(expected,4,32,f)==32);
        time_family_run(input,out);
        if(std::memcmp(expected,out,sizeof out))
        {
            std::fprintf(stderr,"Time family mismatch case %u op%u\n",i,input[0]);
            return 1;
        }
    }
    assert(std::fgetc(f)==EOF);std::fclose(f);
    std::printf("TIME FAMILY ASAN/UBSAN PASS: %u original cases\n",count);
}
#endif
