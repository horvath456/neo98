// SPDX-License-Identifier: GPL-2.0-or-later
// Included in Machine. Only ServiceMicrophone/CloseMicrophone run on the
// Windows/host thread and touch Sound::Microphone. The DOS thread consumes
// a bounded copy under microphone_lock; it never opens an SDL device.
std::mutex microphone_lock;
short microphone_ring[8192]{};
unsigned microphone_head=0, microphone_count=0, microphone_fraction=0, microphone_rate=0;
short microphone_last=0;
std::atomic<unsigned> microphone_requests{0};
unsigned microphone_seen=0, microphone_stream=0;
unsigned long long microphone_time=0;
static void MicrophoneRead(void *owner,unsigned char *out,unsigned count,unsigned rate)
{
    auto &m=*static_cast<Machine *>(owner);
    ++m.microphone_requests;
    std::lock_guard<std::mutex> guard(m.microphone_lock);
    if (!rate)
    {
        if (out && count) out[0]=(m.microphone_last>>8)+128;
        return;
    }
    // The remainder is measured in units of the current DSP rate. A rate
    // change must not use the old denominator to extrapolate past sample b.
    if(m.microphone_rate!=rate){m.microphone_fraction=0;m.microphone_rate=rate;}
    for(unsigned i=0;i<count;++i)
    {
        int value=0;
        if(m.microphone_count)
        {
            const int a=m.microphone_ring[m.microphone_head];
            const int b=m.microphone_count>1?m.microphone_ring[(m.microphone_head+1)%8192]:a;
            value=a+int((long long)(b-a)*m.microphone_fraction/rate);
            m.microphone_fraction+=Sound::InputRate;
            const unsigned skip=m.microphone_fraction/rate;
            m.microphone_fraction%=rate;
            const unsigned consumed=skip<m.microphone_count?skip:m.microphone_count;
            m.microphone_head=(m.microphone_head+consumed)%8192;
            m.microphone_count-=consumed;
            if(consumed<skip)m.microphone_fraction=0;
        }
        out[i]=(value>>8)+128;
    }
}
void CloseMicrophone()
{
    if(microphone_stream)Sound::Microphone::Close(microphone_stream);
    microphone_stream=0;
    std::lock_guard<std::mutex> guard(microphone_lock);
    microphone_count=microphone_head=microphone_fraction=microphone_rate=0;
    microphone_last=0;
}
void ServiceMicrophone(unsigned long long now)
{
    const unsigned requested=microphone_requests.load();
    if(requested!=microphone_seen)
    {
        microphone_seen=requested;
        microphone_time=now;
        if(!microphone_stream && !state_loading)
        {
            microphone_stream=Sound::Microphone::Open();
            if(microphone_stream)Sound::Microphone::Start(microphone_stream,now);
        }
    }
    // Paused, reset or completed DSP capture stops requesting samples. The
    // short grace interval also allows repeated direct-ADC commands.
    if(state_loading || now-microphone_time>250000)
    {
        CloseMicrophone();return;
    }
    if(!microphone_stream)return;
    Sound::Microphone::Advance(now);
    unsigned char bytes[4096];
    const Sound::Format format{Sound::InputRate,1,16};
    const unsigned size=Sound::Microphone::Read(microphone_stream,bytes,sizeof bytes,format);
    std::lock_guard<std::mutex> guard(microphone_lock);
    for(unsigned i=0;i+1<size;i+=2)
    {
        const short value=short(unsigned(bytes[i])|unsigned(bytes[i+1])<<8);
        microphone_last=value;
        // Keep at most about 93 ms. DOS resumes from current input after
        // a pause; stale seconds of captured speech must not be replayed.
        if(microphone_count==4096){microphone_head=(microphone_head+1)%8192;--microphone_count;}
        microphone_ring[(microphone_head+microphone_count++)%8192]=value;
    }
}
