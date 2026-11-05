/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// The C++ runtime of the SDK in an ARM64 program: exceptions through the
// unwinder of the system, RTTI, guarded statics, thread_local, threads and
// locks over what Windows 98 has, and the parts of the standard library the
// ports of ~/dos-ports use (containers, strings, streams, files). A mode on
// the command line ends the program by an exception nobody catches or by a
// function that may not throw. tools/test_arm64_cxx_native.py;
// docs/ARM64-APPS.md, "C++".
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <vector>
#include <windows.h>

static int checks, failures;
static void Check(int id, bool condition)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::printf("FAIL %d\n", id);
}

// --- exceptions ----------------------------------------------------------------
static std::vector<int> trail;
struct Mark
{
    int id;
    explicit Mark(int i) : id(i) {}
    ~Mark() { trail.push_back(id); }
};
struct LoadError : std::runtime_error
{
    int line;
    LoadError(const std::string &what, int l) : std::runtime_error(what), line(l) {}
};
struct Shape
{
    virtual ~Shape() {}
    virtual int Sides() const = 0;
};
struct Square : Shape
{
    int Sides() const override { return 4; }
};
struct Triangle : Shape
{
    int Sides() const override { return 3; }
};
struct Named
{
    virtual ~Named() {}
    std::string name = "named";
};
struct Both : Square, Named
{
};
struct Root
{
    virtual ~Root() {}
    int root = 7;
};
struct Left : virtual Root
{
    int left = 1;
};
struct Right : virtual Root
{
    int right = 2;
};
struct Diamond : Left, Right
{
};

__attribute__((noinline)) static void Throw(int kind)
{
    Mark mark(kind);
    switch (kind)
    {
    case 1: throw 42;
    case 2: throw std::runtime_error("load failed");
    case 3: throw std::string("a text");
    case 4: throw LoadError("bad record", 17);
    case 5: throw static_cast<const char *>("literal");
    case 6: throw 2.5;
    case 7: throw Triangle();
    default: break;
    }
}
__attribute__((noinline)) static int Deep(int depth, int kind)
{
    Mark mark(100 + depth);
    volatile char room[300];
    room[0] = static_cast<char>(depth);
    if (!depth)
    {
        Throw(kind);
        return 0;
    }
    return Deep(depth - 1, kind) + room[0];
}
struct Member
{
    static int alive;
    Member() { ++alive; }
    ~Member() { --alive; }
};
int Member::alive;
struct Half
{
    Member first;
    Member second;
    explicit Half(bool fail)
    {
        if (fail) throw std::logic_error("constructor");
    }
};
static int Parse(const std::string &text)
try
{
    return std::stoi(text);
}
catch (const std::invalid_argument &)
{
    return -1;
}

static void Exceptions()
{
    int caught = 0;
    try { Throw(1); } catch (int e) { caught = e; }
    Check(100, caught == 42);
    std::string what;
    try { Throw(2); } catch (const std::exception &e) { what = e.what(); }
    Check(101, what == "load failed");
    try { Throw(3); } catch (const std::string &s) { what = s; }
    Check(102, what == "a text");
    // A handler for the base takes the derived class, and the object is whole.
    int line = 0;
    try { Throw(4); }
    catch (const std::runtime_error &e)
    {
        what = e.what();
        if (const auto *error = dynamic_cast<const LoadError *>(&e)) line = error->line;
    }
    Check(103, what == "bad record" && line == 17);
    const char *literal = nullptr;
    try { Throw(5); } catch (const char *text) { literal = text; }
    Check(104, literal && !std::strcmp(literal, "literal"));
    double number = 0;
    try { Throw(6); } catch (int) { number = -1; } catch (double d) { number = d; }
    Check(105, number == 2.5);
    int sides = 0;
    try { Throw(7); } catch (const Shape &shape) { sides = shape.Sides(); }
    Check(106, sides == 3);
    bool anything = false;
    try { Throw(3); } catch (int) {} catch (...) { anything = true; }
    Check(107, anything);
    // Nothing thrown: the handler stays out of it.
    try { Throw(0); caught = 1; } catch (...) { caught = 2; }
    Check(108, caught == 1);

    // Destructors on the way: innermost first, each once.
    trail.clear();
    try { Deep(5, 1); } catch (int) { trail.push_back(-1); }
    Check(110, (trail == std::vector<int>{1, 100, 101, 102, 103, 104, 105, -1}));

    // Thrown again from the handler, and a new exception from a handler.
    int outer = 0;
    try
    {
        try { Throw(1); }
        catch (int) { throw; }
    }
    catch (int e) { outer = e; }
    Check(111, outer == 42);
    what.clear();
    try
    {
        try { Throw(2); }
        catch (const std::exception &e) { throw LoadError(std::string("while loading: ") + e.what(), 3); }
    }
    catch (const LoadError &e) { what = e.what(); line = e.line; }
    Check(112, what == "while loading: load failed" && line == 3);

    // A constructor that throws: the members built so far are destroyed.
    bool failed = false;
    try { Half half(true); } catch (const std::logic_error &) { failed = true; }
    Check(113, failed && Member::alive == 0);
    {
        Half half(false);
        Check(114, Member::alive == 2);
    }
    Check(115, Member::alive == 0);
    Check(116, Parse("123") == 123 && Parse("x") == -1);

    // Exceptions of the library itself.
    bool range = false, cast = false, memory = false, length = false;
    std::vector<int> three{1, 2, 3};
    try { (void)three.at(7); } catch (const std::out_of_range &) { range = true; }
    Square square;
    Shape &shape = square;
    try { (void)dynamic_cast<Triangle &>(shape); } catch (const std::bad_cast &) { cast = true; }
    try
    {
        volatile std::size_t huge = static_cast<std::size_t>(-1) / 2;
        char *block = new char[huge];
        block[0] = 0;
        delete[] block;
    }
    catch (const std::bad_alloc &) { memory = true; }
    try { std::string text; text.resize(text.max_size() + 1); } catch (const std::length_error &) { length = true; }
    Check(117, range && cast && memory && length);
    {
        volatile std::size_t huge = static_cast<std::size_t>(-1) / 2;
        Check(118, new (std::nothrow) char[huge] == nullptr);
    }

    // An exception kept and thrown later, and what is in flight.
    std::exception_ptr kept;
    try { Throw(2); } catch (...) { kept = std::current_exception(); }
    what.clear();
    try { std::rethrow_exception(kept); } catch (const std::runtime_error &e) { what = e.what(); }
    Check(119, what == "load failed");
    struct Watcher
    {
        int *seen;
        ~Watcher() { *seen = std::uncaught_exceptions(); }
    };
    int in_flight = -1, at_rest = -1;
    try { Watcher w{&in_flight}; Throw(1); } catch (int) {}
    { Watcher w{&at_rest}; }
    Check(120, in_flight == 1 && at_rest == 0);

    // Many: nothing piles up.
    int total = 0;
    for (int i = 0; i < 20000; ++i)
    {
        try { Mark mark(i); if (i % 2) throw i; throw std::runtime_error("odd"); }
        catch (int e) { total += e & 1; }
        catch (const std::exception &) { total += 2; }
    }
    Check(121, total == 10000 + 20000);
    trail.clear();
}

// --- RTTI ----------------------------------------------------------------------
static void Types()
{
    std::unique_ptr<Shape> shapes[2] = {std::make_unique<Square>(), std::make_unique<Triangle>()};
    Check(200, dynamic_cast<Square *>(shapes[0].get()) && !dynamic_cast<Square *>(shapes[1].get()));
    const Shape &first = *shapes[0], &second = *shapes[1];
    Check(201, typeid(first) == typeid(Square) && typeid(second) != typeid(Square));
    Check(202, std::string(typeid(second).name()).find("Triangle") != std::string::npos);
    Both both;
    Shape *as_shape = &both;
    Named *across = dynamic_cast<Named *>(as_shape);
    Check(203, across && across->name == "named" && dynamic_cast<Both *>(across) == &both);
    Diamond diamond;
    Left *left = &diamond;
    Right *right = dynamic_cast<Right *>(left);
    Root *root = left;
    Check(204, right && right->right == 2 && dynamic_cast<Diamond *>(root) == &diamond && root->root == 7);
    Check(205, dynamic_cast<void *>(root) == static_cast<void *>(&diamond));
}

// --- statics, thread_local, threads --------------------------------------------
static std::atomic<int> constructed{0};
struct Counted
{
    int value;
    Counted() : value(++constructed) {}
};
static Counted &Once()
{
    static Counted one;
    return one;
}
static int FailsTwice()
{
    static int attempts;
    struct Fragile
    {
        Fragile() { if (++attempts < 3) throw attempts; }
    };
    try
    {
        static Fragile fragile;   // an initializer that throws runs again
    }
    catch (int) { return -1; }
    return attempts;
}
struct PerThread
{
    static std::atomic<int> made, destroyed;
    int id;
    PerThread() : id(++made) {}
    ~PerThread() { ++destroyed; }
};
std::atomic<int> PerThread::made{0}, PerThread::destroyed{0};
thread_local PerThread per_thread;
thread_local int plain_local = 5;
__thread int gnu_local = 9;   // the spelling of GCC

static void Threads()
{
    Check(300, Once().value == 1 && Once().value == 1 && constructed == 1);
    Check(301, FailsTwice() == -1 && FailsTwice() == -1 && FailsTwice() == 3 && FailsTwice() == 3);
    Check(302, plain_local == 5 && gnu_local == 9);
    plain_local = 6;
    gnu_local = 10;
    const int main_id = per_thread.id;

    std::mutex mutex;
    std::condition_variable ready;
    int counter = 0, started = 0;
    bool go = false;
    std::vector<int> ids(4), locals(4), declspecs(4);
    std::vector<std::string> errors(4);
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back([&, i] {
            ids[i] = per_thread.id;
            locals[i] = plain_local++;
            declspecs[i] = gnu_local++;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ++started;
                ready.notify_all();
                ready.wait(lock, [&] { return go; });
            }
            for (int n = 0; n < 2000; ++n)
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++counter;
            }
            // An exception inside a thread, with its own unwinding.
            try { Deep(3, 2); } catch (const std::exception &e) { errors[i] = e.what(); }
            (void)Once();
        });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return started == 4; });
        go = true;
        ready.notify_all();
    }
    for (auto &thread : threads) thread.join();
    Check(303, counter == 8000);
    std::set<int> distinct(ids.begin(), ids.end());
    Check(304, distinct.size() == 4 && !distinct.count(main_id));
    Check(305, std::all_of(locals.begin(), locals.end(), [](int v) { return v == 5; }) &&
                   std::all_of(declspecs.begin(), declspecs.end(), [](int v) { return v == 9; }));
    Check(306, plain_local == 6 && gnu_local == 10);
    Check(307, std::all_of(errors.begin(), errors.end(), [](const std::string &e) { return e == "load failed"; }));
    // A thread of the library destroys its thread_local objects when it ends.
    Check(308, PerThread::made == 5 && PerThread::destroyed == 4 && constructed == 1);

    std::once_flag flag;
    int runs = 0;
    std::thread a([&] { std::call_once(flag, [&] { ++runs; }); });
    std::thread b([&] { std::call_once(flag, [&] { ++runs; }); });
    a.join();
    b.join();
    Check(309, runs == 1);

    std::timed_mutex timed;
    timed.lock();
    bool got = true;
    std::thread waiter([&] { got = timed.try_lock_for(std::chrono::milliseconds(30)); });
    waiter.join();
    timed.unlock();
    Check(310, !got);
    {
        std::unique_lock<std::mutex> lock(mutex);
        Check(311, ready.wait_for(lock, std::chrono::milliseconds(20)) == std::cv_status::timeout);
    }
    const auto before = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto slept = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count();
    Check(312, slept >= 30 && slept < 3000);
    Check(313, std::this_thread::get_id() != std::thread::id());
    // The clock of the system and time of the C runtime are the same clock.
    Check(314, std::llabs(static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())) -
                          static_cast<long long>(std::time(nullptr))) <= 2);
    // A recursive lock knows its thread without asking the system: the same
    // thread takes it again, another one does not get it until it is free,
    // and two threads that count under it lose nothing.
    {
        std::recursive_mutex again;
        bool other = true, later = false;
        again.lock();
        again.lock();
        std::thread a([&] { other = again.try_lock(); if (other) again.unlock(); });
        a.join();
        again.unlock();
        std::thread b([&] { other = other || again.try_lock(); });
        b.join();
        again.unlock();
        std::thread c([&] { later = again.try_lock(); if (later) again.unlock(); });
        c.join();
        Check(315, !other && later);
        long plain = 0;
        auto count = [&] {
            for (int i = 0; i < 20000; ++i)
            {
                std::lock_guard<std::recursive_mutex> outer(again);
                std::lock_guard<std::recursive_mutex> inner(again);
                plain = plain + 1;
            }
        };
        std::thread d(count), e(count);
        d.join();
        e.join();
        Check(316, plain == 40000);
    }
}

// --- memory -----------------------------------------------------------------------
struct alignas(64) Wide
{
    char bytes[100];
};
static void Memory()
{
    // operator new with an alignment, and without memory.
    Wide *wide       = new Wide;
    Wide *many       = new Wide[7];
    char *page       = new (std::align_val_t(4096)) char[10];
    const bool lined = !(reinterpret_cast<std::uintptr_t>(wide) & 63) && !(reinterpret_cast<std::uintptr_t>(many) & 63) &&
                       !(reinterpret_cast<std::uintptr_t>(page) & 4095);
    std::memset(many, 1, sizeof(Wide) * 7);
    delete wide;
    delete[] many;
    ::operator delete[](page, std::align_val_t(4096));
    Check(600, lined);
    // More than 4 GiB, which the system below cannot be asked for, and more
    // than there is.
    bool thrown = false, aligned_thrown = false;
    volatile std::size_t beyond = 0x140000000ull;
    try { char *block = new char[beyond]; block[0] = 0; delete[] block; } catch (const std::bad_alloc &) { thrown = true; }
    try { char *block = new (std::align_val_t(64)) char[beyond]; block[0] = 0; ::operator delete[](block, std::align_val_t(64)); }
    catch (const std::bad_alloc &) { aligned_thrown = true; }
    Check(601, thrown && aligned_thrown && new (std::nothrow) char[beyond] == nullptr);
    // Containers that grow and shrink: the blocks come and go inside the
    // program.
    {
        std::vector<std::string> names;
        std::map<int, std::vector<int>> table;
        for (int i = 0; i < 20000; ++i)
        {
            names.push_back("name number " + std::to_string(i) + std::string(static_cast<std::size_t>(i % 90), 'x'));
            table[i % 500].push_back(i);
        }
        long sum = 0;
        for (const auto &row : table) sum += static_cast<long>(row.second.size());
        Check(602, names.size() == 20000 && names[19999].compare(0, 17, "name number 19999") == 0 && sum == 20000);
        std::vector<int> large(3000000, 7);
        large.resize(6000000, 9);
        Check(603, large[0] == 7 && large[5999999] == 9);
    }
}

// --- the library ---------------------------------------------------------------
static void Library(const char *directory)
{
    std::vector<int> numbers{5, 3, 9, 1, 7};
    std::sort(numbers.begin(), numbers.end());
    Check(400, (numbers == std::vector<int>{1, 3, 5, 7, 9}));
    std::map<std::string, int> ages{{"ada", 36}, {"bob", 41}};
    ages["cy"] = 7;
    std::unordered_map<int, std::string> names;
    for (int i = 0; i < 1000; ++i) names[i] = std::to_string(i * i);
    std::deque<int> queue;
    for (int i = 0; i < 100; ++i) { queue.push_back(i); queue.push_front(-i); }
    std::set<std::string> words{"pear", "apple", "fig"};
    Check(401, ages.size() == 3 && ages.begin()->first == "ada" && names[31] == "961" && queue.size() == 200 &&
                   queue.front() == -99 && *words.begin() == "apple");
    std::string text = "The quick brown fox";
    Check(402, text.find("brown") == 10 && text.substr(4, 5) == "quick" && text + "!" == "The quick brown fox!" &&
                   std::string(40, 'x').size() == 40);
    std::function<int(int)> twice = [](int v) { return v * 2; };
    auto shared = std::make_shared<std::vector<int>>(3, 7);
    std::weak_ptr<std::vector<int>> weak = shared;
    Check(403, twice(21) == 42 && shared.use_count() == 1 && weak.lock()->size() == 3);
    shared.reset();
    Check(404, weak.expired());

    std::ostringstream out;
    out << "n=" << 42 << " f=" << 3.25 << " x=" << std::hex << 255 << std::dec << " w=" << std::setw(5) << std::setfill('0')
        << 42 << " p=" << std::fixed << std::setprecision(3) << 2.0 / 3 << " b=" << std::boolalpha << true;
    Check(405, out.str() == "n=42 f=3.25 x=ff w=00042 p=0.667 b=true");
    std::istringstream in("17 -4 2.5 word 0x1f rest of line");
    int a = 0, b = 0, hex = 0;
    double d = 0;
    std::string word, rest;
    in >> a >> b >> d >> word >> std::hex >> hex;
    std::getline(in, rest);
    Check(406, a == 17 && b == -4 && d == 2.5 && word == "word" && hex == 31 && rest == " rest of line");
    Check(407, std::stoi("42") == 42 && std::stod("0.1") == 0.1 && std::to_string(-7) == "-7" &&
                   std::stoul("ff", nullptr, 16) == 255 && std::to_string(1.5) == "1.500000");
    std::mt19937 engine(12345);
    Check(408, engine() == 3992670690u && engine() == 3823185381u);
    std::wstring wide = L"wide";
    std::wostringstream wout;
    wout << wide << L' ' << 12;
    Check(409, wide.size() == 4 && wout.str() == L"wide 12" && std::to_wstring(34) == L"34");

    // Files: text and binary, and a file that is not there.
    const std::string base = std::string(directory) + "\\";
    {
        std::ofstream file(base + "CXXTEXT.TXT");
        file << "first line\n" << 123 << ' ' << 4.5 << "\nlast";
        Check(410, file.good());
    }
    {
        std::ifstream file(base + "CXXTEXT.TXT");
        std::string first, last;
        int n = 0;
        double f = 0;
        std::getline(file, first);
        file >> n >> f >> last;
        Check(411, first == "first line" && n == 123 && f == 4.5 && last == "last" && file.eof());
    }
    {
        // The text file has CR LF on the disk.
        std::ifstream file(base + "CXXTEXT.TXT", std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Check(412, raw == "first line\r\n123 4.5\r\nlast");
    }
    {
        std::vector<unsigned char> bytes(5000);
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(i * 7);
        std::ofstream file(base + "CXXDATA.BIN", std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        std::ifstream back(base + "CXXDATA.BIN", std::ios::binary);
        back.seekg(0, std::ios::end);
        const auto size = back.tellg();
        back.seekg(4000);
        char probe[4];
        back.read(probe, 4);
        Check(413, size == 5000 && static_cast<unsigned char>(probe[0]) == bytes[4000] &&
                       static_cast<unsigned char>(probe[3]) == bytes[4003]);
    }
    // The way the ports report a missing data file.
    std::string error;
    try
    {
        std::ifstream file(base + "MISSING.DAT", std::ios::binary);
        if (!file) throw std::runtime_error("cannot open MISSING.DAT");
    }
    catch (const std::exception &e) { error = e.what(); }
    Check(414, error == "cannot open MISSING.DAT");
    std::remove((base + "CXXTEXT.TXT").c_str());
    std::remove((base + "CXXDATA.BIN").c_str());
    std::error_code code(ERROR_FILE_NOT_FOUND, std::system_category());
    Check(415, code == std::errc::no_such_file_or_directory && !code.message().empty());
}

struct Last
{
    ~Last() { std::printf("static destroyed\n"); }
};
static Last last;
static std::string early = std::string("built ") + "before main";

__attribute__((noinline)) static void MayNotThrow() noexcept { Throw(2); }

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "uncaught")
    {
        std::printf("before\n");
        throw std::runtime_error("nobody catches this");
    }
    if (mode == "noexcept")
    {
        std::printf("before\n");
        MayNotThrow();
        std::printf("after\n");
        return 0;
    }
    if (mode == "terminate")
    {
        std::set_terminate([] {
            std::printf("own terminate\n");
            std::fflush(stdout);
            std::_Exit(7);
        });
        std::printf("before\n");
        throw 5;
    }
    Check(1, early == "built before main");
    Exceptions();
    Types();
    Threads();
    Memory();
    Library(argc > 2 ? argv[2] : "C:\\WINDOWS\\TEMP");
    std::printf("CXX checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
