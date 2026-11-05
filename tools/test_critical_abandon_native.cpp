// SPDX-License-Identifier: GPL-2.0-or-later
// Share a critical-section page between two Unicorn machines. The recursive
// owner exits; a waiting process must acquire the section rather than block
// forever.
#define private public
#include "../src/x86_app.h"
#undef private
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void require(bool ok, const char *why)
{
    if (!ok) { fprintf(stderr, "CRITICAL ABANDON FAILED: %s\n", why); exit(1); }
}
static std::vector<unsigned char> load(const char *p)
{
    FILE *f = fopen(p, "rb");
    require(f, "input");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n);
    require(fread(b.data(), 1, n, f) == unsigned(n), "read input");
    fclose(f);
    return b;
}
static void log(const char *s) { puts(s); }
int main(int argc, char **argv)
{
    require(argc == 3, "usage: runner CRITICALABANDON.EXE fixedsys.fnt");
    auto exe = load(argv[1]), font = load(argv[2]);
    void *backing = nullptr;
    // A whole 16 KiB host page: FEXCore shares host pages, so the backing must
    // lie at the guest address's offset in its 16 KiB page.
    require(!posix_memalign(&backing, 16384, 16384), "shared page allocation");
    for (unsigned i = 0; i < 4096; ++i) static_cast<unsigned *>(backing)[i] = 0;
    auto *producer = new X86::Application(log);
    auto *consumer = new X86::Application(log);
    require(producer->Start(exe.data(), exe.size(), font.data(), font.size(),
                            "CRITICALABANDON.EXE", "producer"), "producer start");
    require(consumer->Start(exe.data(), exe.size(), font.data(), font.size(),
                            "CRITICALABANDON.EXE", "consumer"), "consumer start");
    require(producer->cpu->MapHost(0x50000000, 4096, X86::Prot::All, backing) == X86::CpuError::Ok,
            "producer shared backing");
    require(consumer->cpu->MapHost(0x50000000, 4096, X86::Prot::All, backing) == X86::CpuError::Ok,
            "consumer shared backing");
    require(producer->virtual_memory.Track(0x50000000, 4096, 4) &&
                consumer->virtual_memory.Track(0x50000000, 4096, 4),
            "shared page access metadata");
    volatile unsigned *shared = static_cast<volatile unsigned *>(backing);
    bool blocked = false, abandoned = false;
    unsigned producer_thread = 0, producer_calls = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < end && consumer->Status() != X86::Application::Exited)
    {
        if (producer)
        {
            producer->Pump(1);
            require(producer->Status() != X86::Application::Failed, "producer runtime");
        }
        // The end of the producer is looked at before the consumer runs
        // again: a turn lasts up to 4 ms since 7253727, long enough for the
        // consumer to take the lock, finish and delete it in one pump.
        if (producer && producer->Status() == X86::Application::Exited)
        {
            require(!producer->ExitCode(), "producer guest checks");
            require(blocked, "the consumer was waiting before the producer ended");
            producer_calls = producer->ApiCalls();
            delete producer;
            producer = nullptr;
            auto *p = consumer->FindCritical(shared[1]);
            require(p, "the global lock survives the producer");
            // Ownership depends on scheduling, but the departed producer must
            // no longer own it after the consumer runs.
            printf("CRITICAL ABANDON after exit owner=%u recursion=%u waiting=%u\n",
                   p->data.owner, p->data.recursion, p->waiting);
            abandoned = true;
        }
        consumer->Pump(1);
        require(consumer->Status() != X86::Application::Failed, "consumer runtime");
        if (!blocked && consumer->threads[0].block == 5)
        {
            require(producer != nullptr, "the producer still owns the lock while the consumer waits");
            auto *p = producer->FindCritical(shared[1]);
            require(p && consumer->FindCritical(shared[1]) == p, "one native lock in both processes");
            producer_thread = producer->threads[0].id;
            require(p->data.owner == producer_thread && p->data.recursion == 2 && p->waiting == 1,
                    "the producer holds it twice and the consumer is queued");
            blocked = true;
        }
    }
    require(blocked && abandoned, "contention and creator teardown observed");
    require(consumer->Status() == X86::Application::Exited && !consumer->ExitCode() && shared[0] == 7,
            "the consumer took the abandoned lock and finished");
    require(!consumer->FindCritical(shared[1]), "the object is gone after DeleteCriticalSection");
    printf("CRITICAL ABANDON PASS producer_calls=%u consumer_calls=%u object=%08x\n",
           producer_calls, consumer->ApiCalls(), shared[1]);
    delete consumer;
    free(backing);
    return 0;
}
