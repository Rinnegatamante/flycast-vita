#pragma once

// Diagnostic only. Arguments and clock reads disappear in ordinary builds.
#ifdef VITA_FRAME_TRACE
#include <cstdint>
namespace frame_trace {
enum Lane { Emu, Render };
enum Kind { Compile, CacheClear, TempClear, BlockCheck, RamInvalidate,
    GuestFrame, QueueResult, QueueWait, RenderWait, RenderBegin,
    Process, Draw, Present, Presented, Texture, GuestSecond, VBlank };
void init();
void bindEmu();
void poll(); // Emulation thread only; exports after the capture deadline.
uint64_t begin();
void end(Lane lane, Kind kind, uint64_t start, uint32_t arg);
void mark(Lane lane, Kind kind, uint32_t arg, uint64_t detail = 0);
struct Scope {
    Lane lane;
    Kind kind;
    uint32_t arg;
    uint64_t start;
    Scope(Lane l, Kind k, uint32_t a) : lane(l), kind(k), arg(a), start(begin()) {}
    ~Scope() { if (start) end(lane, kind, start, arg); }
};
}
#define FT_JOIN_INNER(a, b) a##b
#define FT_JOIN(a, b) FT_JOIN_INNER(a, b)
#define FT_SCOPE(l, k, a) frame_trace::Scope FT_JOIN(ft_scope_, __LINE__)(frame_trace::l, frame_trace::k, a)
#define FT_MARK(l, k, a, d) frame_trace::mark(frame_trace::l, frame_trace::k, a, d)
#define FT_INIT() frame_trace::init()
#define FT_BIND_EMU() frame_trace::bindEmu()
#define FT_POLL() frame_trace::poll()
#else
#define FT_SCOPE(l, k, a) do {} while (0)
#define FT_MARK(l, k, a, d) do {} while (0)
#define FT_INIT() do {} while (0)
#define FT_BIND_EMU() do {} while (0)
#define FT_POLL() do {} while (0)
#endif
