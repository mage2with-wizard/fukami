// Ported from src/product/patches.cpp (NA rrv_native), JP addresses via jp-experiment/native/.
// RR5 native hot functions (owner direction 2026-09-30), RRV_RR5_NATIVE_HOT=1.
// The most expensive guest functions (host profile of the game thread, boot
// through the demo race) run as native versions made by tools/ee-native from
// their own generated code: every instruction statement is kept, but the two
// runtime calls the generated code makes before each instruction
// (gate3CheckpointV1, gate3BeginInstructionV1) become a local cycle counter
// inside the lazy window, committed before any runtime interaction. The guest
// sees the same state at every interaction, so guest time and guest data are
// unchanged; only host time drops. A function whose code in guest RAM differs
// from the words it was generated from falls back to the generated original.
#include <array>
#include <utility>
namespace rrv_native {
struct Word { uint32_t pc, word; };
using Fn = void (*)(uint8_t*, R5900Context*, PS2Runtime*);
struct Entry { uint32_t address; const char* name; Fn original; Fn native; const Word* words; size_t count; };
// Same switch as the runtime's rrvSprFastV1 (RRV_GATE4_SPR_FAST=0 disables).
inline const bool kSprFast = [] { const char* v = std::getenv("RRV_GATE4_SPR_FAST"); return !(v && v[0] == '0'); }();
// The clock of one native function. Its fields must stay in host registers: a native function runs
// the checkpoint test before every guest instruction, and guest stores (through uint8_t*) may alias
// anything whose address has escaped. So no member function that is not inlined takes `this`, and
// nothing outside is handed a reference: the slow paths receive and return the counters by value
// (State), and hooks get the scalars they need. Host disassembly of 2026-10-02 (the address did
// escape, through the out-of-line slow path): five loads and three stores of clock fields per
// guest instruction.
struct Clock {
    struct State { uint64_t c, base, lim, gen, lastck; bool any; };
    PS2Runtime* rt; R5900Context* ctx; uint8_t* spr_;
    uint64_t c = 0, base = 0, lim = 0, gen = 0, lastck = 0; bool any = false;
    __attribute__((always_inline)) Clock(PS2Runtime* r, R5900Context* x) : rt(r), ctx(x), spr_(r->memory().getScratchpad()) { reload(); }
    __attribute__((always_inline)) ~Clock() { sync(); }
    Clock(const Clock&) = delete; Clock& operator=(const Clock&) = delete;
    __attribute__((always_inline)) inline State state() const { return State{c, base, lim, gen, lastck, any}; }
    __attribute__((always_inline)) inline void set(const State& s) { c = s.c; base = s.base; lim = s.lim; gen = s.gen; lastck = s.lastck; any = s.any; }
    // Re-read the runtime after it may have run: cycle count and lazy window.
    __attribute__((always_inline)) inline void reload() { uint64_t g = 0; c = base = rt->gate4NativeCyclesV1(); lim = rt->gate4NativeLimitV1(ctx, g); gen = g; any = false; }
    // Publish the local count exactly as the per-instruction calls would have.
    __attribute__((always_inline)) inline void sync() { if (c != base || any) { rt->gate4NativeCommitV1(ctx, c, lastck, any); base = c; any = false; } }
    __attribute__((always_inline)) inline bool quiet() const { return rrv::guest_time::quiet_generation.load(std::memory_order_relaxed) == gen; }
    // gate3CheckpointV1: the lazy fast path, else the real checkpoint.
    // Always inline: in the largest function the compiler called it instead (199 of its sites, Steam Deck
    // profile 2026-10-02), a call and a return around two compares.
    __attribute__((always_inline)) inline void ck() { if (__builtin_expect(c < lim && quiet(), true)) { lastck = c; any = true; } else set(slow(rt, ctx, state())); }
    __attribute__((always_inline)) inline void begin() { ++c; }
    // Back-edge scheduler check: a no-op while the armed window's generation holds.
    __attribute__((always_inline)) inline bool preempt() {
        if (lim != 0 && quiet()) return false;
        sync(); const bool r = rt->shouldPreemptGuestExecution(); reload(); return r;
    }
    __attribute__((always_inline)) inline void signal(R5900Context* x, PS2Exception e) { set(signalSlow(rt, ctx, x, e, state())); }
    __attribute__((always_inline)) inline void vu0(uint8_t* rdram, R5900Context* x, uint32_t address) { sync(); rt->executeVU0Microprogram(rdram, x, address); reload(); }
    __attribute__((always_inline)) inline void brk(uint8_t* rdram, R5900Context* x) { set(brkSlow(rt, ctx, rdram, x, state())); }
    // The runtime's scratchpad fast path (rrvSprFastV1), inline.
    __attribute__((always_inline)) inline uint8_t* spr(uint32_t vaddr, uint32_t size) const {
        const uint32_t offset = vaddr - PS2_SCRATCHPAD_BASE;
        if (!kSprFast || g_ps2PathWatchArmed || offset >= PS2_SCRATCHPAD_SIZE || (vaddr & (size - 1u)) != 0u) return nullptr;
        return spr_ ? spr_ + offset : nullptr;
    }
private:
    // The slow paths: commit, call the runtime, re-read. By value in and out (see above).
    static inline void commit(PS2Runtime* rt, R5900Context* ctx, State& s) {
        if (s.c != s.base || s.any) { rt->gate4NativeCommitV1(ctx, s.c, s.lastck, s.any); s.base = s.c; s.any = false; }
    }
    static inline void reread(PS2Runtime* rt, R5900Context* ctx, State& s) {
        s.c = s.base = rt->gate4NativeCyclesV1(); s.lim = rt->gate4NativeLimitV1(ctx, s.gen); s.any = false;
    }
    __attribute__((noinline)) static State slow(PS2Runtime* rt, R5900Context* ctx, State s) { commit(rt, ctx, s); rt->gate3CheckpointV1(ctx); reread(rt, ctx, s); return s; }
    __attribute__((noinline)) static State signalSlow(PS2Runtime* rt, R5900Context* ctx, R5900Context* x, PS2Exception e, State s) { commit(rt, ctx, s); rt->SignalException(x, e); reread(rt, ctx, s); return s; }
    __attribute__((noinline)) static State brkSlow(PS2Runtime* rt, R5900Context* ctx, uint8_t* rdram, R5900Context* x, State s) { commit(rt, ctx, s); rt->handleBreak(rdram, x); reread(rt, ctx, s); return s; }
};
} // namespace rrv_native
// Hand-written native routines the generated native functions hook (tools/ee-native --hook).
#include "rr5_car_builder_jp.h" // func_222F00's block loop: one VCALLMS per car vertex
#pragma push_macro("READ8")
#pragma push_macro("READ16")
#pragma push_macro("READ32")
#pragma push_macro("READ64")
#pragma push_macro("READ128")
#pragma push_macro("WRITE8")
#pragma push_macro("WRITE16")
#pragma push_macro("WRITE32")
#pragma push_macro("WRITE64")
#pragma push_macro("WRITE128")
#undef READ8
#undef READ16
#undef READ32
#undef READ64
#undef READ128
#undef WRITE8
#undef WRITE16
#undef WRITE32
#undef WRITE64
#undef WRITE128
#define RRV_NATIVE_READ(T, N, addr) ([&]() -> T { const uint32_t _a = (uint32_t)(addr); \
    if (!PS2Runtime::isSpecialAddress(_a)) return FAST_READ##N(_a); \
    if (uint8_t* _p = rrvNative.spr(_a, sizeof(T))) { T _v; std::memcpy(&_v, _p, sizeof(T)); return _v; } \
    rrvNative.sync(); const T _v = runtime->Load##N(rdram, ctx, _a); rrvNative.reload(); return _v; }())
#define READ8(addr) RRV_NATIVE_READ(uint8_t, 8, addr)
#define READ16(addr) RRV_NATIVE_READ(uint16_t, 16, addr)
#define READ32(addr) RRV_NATIVE_READ(uint32_t, 32, addr)
#define READ64(addr) RRV_NATIVE_READ(uint64_t, 64, addr)
#define READ128(addr) RRV_NATIVE_READ(__m128i, 128, addr)
#define RRV_NATIVE_WRITE(T, N, NAME, addr, val) do { const uint32_t _a = (addr); \
    if (PS2Runtime::isSpecialAddress(_a)) { \
        if (uint8_t* _p = rrvNative.spr(_a, sizeof(T))) { const T _v = (T)(val); std::memcpy(_p, &_v, sizeof(T)); } \
        else { rrvNative.sync(); runtime->Store##N(rdram, ctx, _a, (val)); rrvNative.reload(); } } \
    else { ps2TraceGuestWrite(rdram, _a, sizeof(T), (T)(val), 0u, NAME, ctx); FAST_WRITE##N(_a, (val)); } } while (0)
#define WRITE8(addr, val) RRV_NATIVE_WRITE(uint8_t, 8, "WRITE8", addr, val)
#define WRITE16(addr, val) RRV_NATIVE_WRITE(uint16_t, 16, "WRITE16", addr, val)
#define WRITE32(addr, val) RRV_NATIVE_WRITE(uint32_t, 32, "WRITE32", addr, val)
#define WRITE64(addr, val) RRV_NATIVE_WRITE(uint64_t, 64, "WRITE64", addr, val)
#define WRITE128(addr, val) do { const uint32_t _a = (addr); const __m128i _value = (val); \
    if (PS2Runtime::isSpecialAddress(_a)) { \
        if (uint8_t* _p = rrvNative.spr(_a, 16u)) { std::memcpy(_p, &_value, 16u); } \
        else { rrvNative.sync(); runtime->Store128(rdram, ctx, _a, _value); rrvNative.reload(); } } \
    else { const uint64_t _lo = static_cast<uint64_t>(PS2_EXTRACT_EPI64_0(_value)); \
        const uint64_t _hi = static_cast<uint64_t>(PS2_EXTRACT_EPI64_1(_value)); \
        ps2TraceGuestWrite(rdram, _a, 16u, _lo, _hi, "WRITE128", ctx); FAST_WRITE128(_a, _value); } } while (0)
#include "native/rrv_ee_native.inc"  // game-derived native hot functions: generated/rr5/native (RRV_GENERATED_INCLUDE_DIR)
#undef RRV_NATIVE_READ
#undef RRV_NATIVE_WRITE
#pragma pop_macro("READ8")
#pragma pop_macro("READ16")
#pragma pop_macro("READ32")
#pragma pop_macro("READ64")
#pragma pop_macro("READ128")
#pragma pop_macro("WRITE8")
#pragma pop_macro("WRITE16")
#pragma pop_macro("WRITE32")
#pragma pop_macro("WRITE64")
#pragma pop_macro("WRITE128")
namespace rrv_native {
constexpr size_t kCount = sizeof(rrv_native_entries) / sizeof(rrv_native_entries[0]);
uint64_t g_calls[kCount] = {}, g_fallbacks = 0;
bool wordsMatch(const uint8_t* rdram, const Entry& e) {
    for (size_t i = 0; i < e.count; ++i) {
        const uint8_t* p = getMemPtr(const_cast<uint8_t*>(rdram), e.words[i].pc);
        uint32_t w = 0;
        if (!p) return false;
        std::memcpy(&w, p, 4);
        if (w != e.words[i].word) return false;
    }
    return true;
}
template <size_t I>
void thunk(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static int state = 0; // 0 unchecked, 1 native, 2 original
    const Entry& e = rrv_native_entries[I];
    if (__builtin_expect(state != 1, false)) {
        if (state == 0) {
            state = wordsMatch(rdram, e) ? 1 : 2;
            if (state == 2) {
                ++g_fallbacks;
                std::fprintf(stderr, "[rr5-native] %s: guest code differs from its generation words; original kept\n", e.name);
            }
        }
        if (state == 2) { e.original(rdram, ctx, runtime); return; }
    }
    ++g_calls[I];
    e.native(rdram, ctx, runtime);
}
template <size_t... I>
constexpr std::array<Fn, sizeof...(I)> thunks(std::index_sequence<I...>) { return {&thunk<I>...}; }
void registerAll(PS2Runtime& runtime) {
    static constexpr auto table = thunks(std::make_index_sequence<kCount>{});
    size_t n = 0, wrapped = 0;
    // RRV_RR5_NATIVE_COUNT=N (developer): only the first N functions of the list, to find the one behind
    // a difference by halving.
    size_t count = kCount;
    if (const char* v = std::getenv("RRV_RR5_NATIVE_COUNT"); v && v[0])
        count = std::min<size_t>(kCount, static_cast<size_t>(std::strtoull(v, nullptr, 10)));
    for (size_t i = 0; i < count; ++i) {
        const Entry& e = rrv_native_entries[i];
        if (!runtime.hasFunction(e.address)) continue;
        // Only replace the generated original, never another patch.
        if (runtime.lookupFunction(e.address) != e.original) {
            // A widescreen reader wrapper (patch_ws_reader) holds the address and calls the function it
            // replaced through a saved pointer: that call takes the native version.
            for (uint32_t r = 0; r < kWsReaderCount; ++r)
                if (kWsReaders[r] == e.address && g_wsReaderOrig[r] == e.original) { g_wsReaderOrig[r] = table[i]; ++wrapped; }
            continue;
        }
        runtime.registerFunction(e.address, table[i]);
        ++n;
    }
    std::fprintf(stderr, "[rr5-native] native hot functions: %zu of %zu registered, %zu more behind a widescreen wrapper\n",
                 n, kCount, wrapped);
    std::fprintf(stderr, "[rr5-car] native vertex block loop (func_222F00): %s\n",
                 !rrv_car::kEnabled ? "off (RRV_RR5_NATIVE_BUILDER=0)" : rrv_car::kVerify ? "verify" : "on");
    static const bool atExit = [] { std::atexit([] {
        std::fprintf(stderr, "[rr5-native] calls:");
        for (size_t i = 0; i < kCount; ++i)
            std::fprintf(stderr, " %s=%llu", rrv_native_entries[i].name, (unsigned long long)g_calls[i]);
        std::fprintf(stderr, " fallbacks=%llu\n", (unsigned long long)g_fallbacks);
        rrv_car::reportAtExit();
    }); return true; }();
    (void)atExit;
}
} // namespace rrv_native
