#include "frame_trace.h"
#include "types.h"
#include "cfg/cfg.h"
#include "cfg/option.h"
#include "stdclass.h"
#include <psp2/kernel/processmgr.h>
#include <atomic>
#include <algorithm>
#include <vector>
#include <cstdio>

namespace frame_trace {
struct Event {
    uint64_t start, end, detail;
    uint32_t kind, arg;
};
struct Stream {
    std::vector<Event> events;
    std::atomic<uint32_t> published{0};
    std::atomic<uint32_t> overflow{0};
};
static Stream streams[2];
static std::atomic<bool> active{false};
static uint64_t origin, deadline;
static bool initialized, exported;
static __thread int currentLane = -1;
static std::atomic<uint32_t> wrongLane{0};
static const char* names[] = { "compile", "cache_clear", "temp_clear", "block_check", "ram_invalidate",
    "guest_frame", "queue_result", "queue_wait", "render_wait", "render_begin",
    "process", "draw", "present", "presented", "texture", "guest_second", "vblank",
    "decode", "analyse", "reg_alloc", "emit", "native_hit", "native_check" };

void init()
{
    // Called before the emulation worker is launched. Never reuse buffers in
    // this process: a late renderer scope must not race a new capture.
    if (initialized || settings.content.gameId != "T1212N")
        return;
    initialized = true;
    if (!cfgLoadBool("perf", "FrameTrace", false) || !config::ThreadedRendering)
        return;
    currentLane = Render;
    streams[Emu].events.resize(81920);
    streams[Render].events.resize(131072);
    int seconds = std::max(5, std::min(120, cfgLoadInt("perf", "FrameTraceSeconds", 30)));
    origin = sceKernelGetProcessTimeWide();
    deadline = origin + uint64_t(seconds) * 1000000;
    NOTICE_LOG(COMMON, "frame-trace: start=%llu duration=%d seconds; wall scopes, not CPU/GPU time",
        (unsigned long long)origin, seconds);
    active.store(true, std::memory_order_release);
}

void bindEmu()
{
    currentLane = Emu;
}

uint64_t begin()
{
    if (!active.load(std::memory_order_acquire))
        return 0;
    uint64_t now = sceKernelGetProcessTimeWide();
    return now < deadline ? now : 0;
}

static void append(Lane lane, Kind kind, uint64_t start, uint64_t finish, uint32_t arg, uint64_t detail)
{
    // One writer per lane, immutable append-only prefix. Publication allows
    // the exporter to copy a frozen prefix while the other lane finishes.
    // No locks, allocation, file I/O or overwrites during capture.
    if (currentLane != lane) {
        wrongLane.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    Stream& stream = streams[lane];
    uint32_t index = stream.published.load(std::memory_order_relaxed);
    if (index == stream.events.size()) {
        stream.overflow.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    stream.events[index] = {start - origin, finish - origin, detail, uint32_t(kind), arg};
    stream.published.store(index + 1, std::memory_order_release);
}

void end(Lane lane, Kind kind, uint64_t start, uint32_t arg)
{
    uint64_t finish = begin();
    // Cross-deadline events are omitted, not reported as artificially short.
    if (finish)
        append(lane, kind, start, finish, arg, 0);
}

void mark(Lane lane, Kind kind, uint32_t arg, uint64_t detail)
{
    uint64_t now = begin();
    if (now)
        append(lane, kind, now, now, arg, detail);
}

void poll()
{
    if (!active.load(std::memory_order_acquire) || exported || sceKernelGetProcessTimeWide() < deadline)
        return;
    active.store(false, std::memory_order_release);
    exported = true;
    const uint32_t counts[] = {streams[Emu].published.load(std::memory_order_acquire),
                              streams[Render].published.load(std::memory_order_acquire)};
    constexpr size_t kindCount = sizeof(names) / sizeof(names[0]);
    uint32_t kindCounts[2][kindCount] = {};
    uint64_t kindDurations[2][kindCount] = {};
    for (unsigned lane = 0; lane < 2; ++lane)
        for (uint32_t i = 0; i < counts[lane]; ++i) {
            const Event& event = streams[lane].events[i];
            if (event.kind < kindCount) {
                kindCounts[lane][event.kind]++;
                kindDurations[lane][event.kind] += event.end - event.start;
            }
        }
    NOTICE_LOG(COMMON, "frame-trace: summary emu=%u render=%u overflow=%u/%u wrong_lane=%u",
        counts[Emu], counts[Render], streams[Emu].overflow.load(),
        streams[Render].overflow.load(), wrongLane.load());
    for (unsigned lane = 0; lane < 2; ++lane)
        for (size_t kind = 0; kind < kindCount; ++kind)
            if (kindCounts[lane][kind] != 0)
                NOTICE_LOG(COMMON, "frame-trace: lane=%s kind=%s count=%u total_us=%llu",
                    lane ? "render" : "emu", names[kind], kindCounts[lane][kind],
                    (unsigned long long)kindDurations[lane][kind]);
    std::vector<uint64_t> guestFrameIntervals;
    uint64_t previousGuestFrame = 0;
    const uint64_t captureUs = deadline - origin;
    for (uint32_t i = 0; i < counts[Emu]; ++i) {
        const Event& event = streams[Emu].events[i];
        if (event.kind != GuestFrame || event.start < 1000000
                || event.start >= captureUs - 1000000)
            continue;
        if (previousGuestFrame != 0 && event.start >= previousGuestFrame)
            guestFrameIntervals.push_back(event.start - previousGuestFrame);
        previousGuestFrame = event.start;
    }
    if (!guestFrameIntervals.empty()) {
        std::sort(guestFrameIntervals.begin(), guestFrameIntervals.end());
        const size_t median = guestFrameIntervals.size() / 2;
        const size_t p95 = (guestFrameIntervals.size() * 95 + 99) / 100 - 1;
        NOTICE_LOG(COMMON,
            "frame-trace: guest-frame-intervals count=%u median_us=%llu p95_us=%llu max_us=%llu",
            (unsigned)guestFrameIntervals.size(),
            (unsigned long long)guestFrameIntervals[median],
            (unsigned long long)guestFrameIntervals[p95],
            (unsigned long long)guestFrameIntervals.back());
    }
    // The CSV can take many seconds to serialize on Vita. Summary-only mode
    // preserves a complete, bounded capture when another app may take over.
    if (cfgLoadBool("perf", "FrameTraceSummaryOnly", false))
        return;
    const std::string path = get_writable_data_path("mvc2_frame_trace.csv");
    FILE* file = fopen(path.c_str(), "w");
    if (!file) {
        ERROR_LOG(COMMON, "frame-trace: cannot open %s", path.c_str());
        return;
    }
    fprintf(file, "# version=1 origin_us=%llu duration_us=%llu; omit first/last second; wall intervals include scheduling/waits\n",
        (unsigned long long)origin, (unsigned long long)(deadline - origin));
    fprintf(file, "# emu_count=%u render_count=%u emu_overflow=%u render_overflow=%u wrong_lane=%u\n",
        counts[0], counts[1], streams[0].overflow.load(), streams[1].overflow.load(), wrongLane.load());
    fprintf(file, "lane,kind,start_us,end_us,arg,detail\n");
    for (unsigned lane = 0; lane < 2; ++lane)
        for (uint32_t i = 0; i < counts[lane]; ++i) {
            const Event& event = streams[lane].events[i];
            fprintf(file, "%s,%s,%llu,%llu,%u,%llu\n", lane ? "render" : "emu", names[event.kind],
                (unsigned long long)event.start, (unsigned long long)event.end,
                event.arg, (unsigned long long)event.detail);
        }
    int error = ferror(file);
    int closed = fclose(file);
    NOTICE_LOG(COMMON, "frame-trace: saved emu=%u render=%u overflow=%u/%u io_error=%d path=%s",
        counts[0], counts[1], streams[0].overflow.load(), streams[1].overflow.load(), error || closed, path.c_str());
}
}
