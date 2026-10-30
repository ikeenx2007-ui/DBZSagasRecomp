// Hooks for default.xbe (Dragon Ball Z: Sagas, NTSC, XDK 5849).
//
// Micro-tasks. The Avalanche engine runs its game logic as cooperative tasks, each on its own guest stack:
//   0x0011EF90  TaskContextInit(ctx, entry, stack, offset, size): builds a frame that "returns" into entry
//   0x001FB72C  Switch(to, from): pushes the registers, saves esp/ebp in *from, loads them from *to, pops, ret
//   0x001FB74F  JumpTo(to): the same without saving, used when a task ends (0x0011E2C0)
//   0x0011E370  the scheduler: Switch(task, 0x0030FD90) for each due task; a task yields with Switch(0x30FD90, task)
// A static recompilation returns through the host stack, so the guest esp swap alone cannot move execution to another
// task: ret came back to the caller of Switch with the other task's registers (esi = 0, crash at 0x0010FAAE).
// Each guest context gets a host fiber instead. Switch saves the caller's registers and switches fibers; a context
// that never ran gets a new fiber, which pops the frame TaskContextInit built and enters the task.
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/log.h"
#include "xdk/hle.h"

// after the SDK headers, without the min/max macros that break std::numeric_limits in cpu/x86_ops.h
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace kernel;

namespace {

constexpr uint32_t kCtxEbp = 0, kCtxEsp = 4;  // the guest context: saved ebp, saved esp
constexpr SIZE_T kFiberCommit = 256 * 1024, kFiberReserve = 16 * 1024 * 1024;

struct Regs {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t cf, zf, sf, of, pf, df;
};

struct Task {
    void* fiber = nullptr;
    Regs regs{};         // the registers Switch returns with when the task resumes
    bool owned = false;  // a fiber this file created (not a thread converted to one)
};

struct Start {
    X86Context* ctx;
    uint8_t* base;
    uint32_t key;
};

std::recursive_mutex g_lock;
std::unordered_map<uint32_t, Task> g_tasks;  // guest context address -> its fiber
std::vector<void*> g_dead;                   // fibers of ended tasks, deleted from another fiber
thread_local uint32_t t_current = 0;         // guest context of the running fiber, 0 before the first switch
thread_local void* t_main = nullptr;         // this thread's own fiber, never deleted
std::atomic<uint64_t> g_switches{0}, g_overlays{0}, g_frames{0};  // diagnostics, see DbzInit

void* MainFiber() {
    if (!t_main) t_main = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
    return t_main;
}

Regs Save(const X86Context& c) {
    return {c.eax, c.ecx, c.edx, c.ebx, c.esp, c.ebp, c.esi, c.edi, c.cf, c.zf, c.sf, c.of, c.pf, c.df};
}
void Load(X86Context& c, const Regs& r) {
    c.eax = r.eax, c.ecx = r.ecx, c.edx = r.edx, c.ebx = r.ebx, c.esp = r.esp, c.ebp = r.ebp, c.esi = r.esi,
    c.edi = r.edi, c.cf = r.cf, c.zf = r.zf, c.sf = r.sf, c.of = r.of, c.pf = r.pf, c.df = r.df;
}

void ReapDead() {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    void* self = GetCurrentFiber();
    for (auto it = g_dead.begin(); it != g_dead.end();) {
        if (*it != self) {
            DeleteFiber(*it);
            it = g_dead.erase(it);
        } else {
            ++it;
        }
    }
}

// The guest side of what Switch writes, so code reading a context sees the values the Xbox would leave there
void WriteGuestFrame(X86Context& ctx, uint32_t from) {
    uint32_t esp = ctx.esp;  // at the return address
    uint32_t flags = 0x202 | (ctx.cf & 1) | (ctx.zf & 1) << 6 | (ctx.sf & 1) << 7 | (ctx.of & 1) << 11;
    const uint32_t frame[] = {ctx.edx, ctx.ecx, ctx.ebx, ctx.eax, ctx.edi, ctx.esi, flags, ctx.ebp};
    uint32_t ebp_slot = esp - 4;
    for (int i = 0; i < 8; i++) W32(esp - 4 * uint32_t(8 - i), frame[i]);
    W32(from + kCtxEbp, ebp_slot);
    W32(from + kCtxEsp, esp - 32);
}

void CALLBACK TaskFiber(void* param) {
    Start s = *static_cast<Start*>(param);
    delete static_cast<Start*>(param);
    ReapDead();
    X86Context& ctx = *s.ctx;
    // the frame TaskContextInit built, popped as Switch does: edx ecx ebx eax edi esi flags, leave, ret
    uint32_t esp = R32(s.key + kCtxEsp), ebp = R32(s.key + kCtxEbp);
    ctx.edx = R32(esp), ctx.ecx = R32(esp + 4), ctx.ebx = R32(esp + 8), ctx.eax = R32(esp + 12);
    ctx.edi = R32(esp + 16), ctx.esi = R32(esp + 20);
    uint32_t flags = R32(esp + 24);
    ctx.cf = flags & 1, ctx.zf = flags >> 6 & 1, ctx.sf = flags >> 7 & 1, ctx.of = flags >> 11 & 1;
    ctx.df = flags >> 10 & 1;
    esp = ebp;  // leave
    ctx.ebp = R32(esp);
    uint32_t entry = R32(esp + 4);
    ctx.esp = esp + 8;  // after ret: the entry sees [esp] as its return address
    {
        static std::atomic<int> n{0};
        if (n.fetch_add(1, std::memory_order_relaxed) < 8) LOGR("dbz: task %08X starts at %08X", s.key, entry);
    }
    x86_icall(ctx, s.base, entry);
    // tasks end through JumpTo and never get here
    x86_trap(ctx, entry, "dbz: a micro-task returned from its entry");
}

// leaves the running fiber for the context to; save = keep the caller resumable
void SwitchTo(X86Context& ctx, uint8_t* base, uint32_t to, uint32_t from, bool save) {
    void* target = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        void* main_fiber = MainFiber();
        void* self = GetCurrentFiber();
        if (save) {
            Task& me = g_tasks[from];
            if (me.fiber && me.fiber != self && me.owned) g_dead.push_back(me.fiber);  // a stale task at this context
            me.fiber = self;
            me.owned = self != main_fiber;
            // when resumed: Switch's ret, every register as the call left it
            me.regs = Save(ctx);
            me.regs.esp = ctx.esp + 4;
        } else if (t_current) {
            // an ended task: its fiber goes once another runs
            auto it = g_tasks.find(t_current);
            if (it != g_tasks.end() && it->second.fiber == self) {
                if (it->second.owned) g_dead.push_back(self);
                g_tasks.erase(it);
            }
        }
        auto it = g_tasks.find(to);
        if (it != g_tasks.end() && it->second.fiber) {
            target = it->second.fiber;
        } else {
            target = CreateFiberEx(kFiberCommit, kFiberReserve, FIBER_FLAG_FLOAT_SWITCH, TaskFiber,
                                   new Start{&ctx, base, to});
            if (!target) x86_trap(ctx, R32(ctx.esp), "dbz: CreateFiberEx failed");
            Task& t = g_tasks[to];
            t.fiber = target;
            t.owned = true;
            t.regs = {};  // a new task loads its registers from the frame TaskContextInit built
        }
        t_current = to;
    }
    g_switches.fetch_add(1, std::memory_order_relaxed);
    SwitchToFiber(target);
    // back on this fiber: another task switched to the context saved above
    ReapDead();
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    t_current = from;
    Load(ctx, g_tasks[from].regs);
}

}  // namespace

// Switch(to, from), cdecl
X86_FUNC(sub_001FB72C) {
    uint32_t to = R32(ctx.esp + 4), from = R32(ctx.esp + 8);
    WriteGuestFrame(ctx, from);
    SwitchTo(ctx, base, to, from, true);
    // Load put esp after the return address, as the original's ret leaves it
}

// JumpTo(to), cdecl: the running task is over
X86_FUNC(sub_001FB74F) {
    uint32_t to = R32(ctx.esp + 4);
    SwitchTo(ctx, base, to, 0, false);
    x86_trap(ctx, to, "dbz: an ended micro-task was resumed");
}

// TaskContextInit(ctx, entry, stack, offset, size), cdecl: a context built again belongs to a new task
X86_EXTERN_FUNC(__imp__sub_0011EF90);
X86_FUNC(sub_0011EF90) {
    uint32_t key = R32(ctx.esp + 4);
    {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        auto it = g_tasks.find(key);
        if (it != g_tasks.end() && it->second.fiber != GetCurrentFiber()) {
            if (it->second.owned && it->second.fiber) g_dead.push_back(it->second.fiber);
            g_tasks.erase(it);
        }
    }
    __imp__sub_0011EF90(ctx, base);
    ReapDead();
}

// [[hook]] at 0x0014E750, in the movie player's cleanup: the inlined D3DDevice_Release compares the reference count
// with 1 and on the last reference tears the device down (0x002570D0). True jumps to the decrement instead.
bool KeepD3DDevice(X86Context& ctx, uint8_t*) {
    LOGR("dbz: movie player releases the device (count %u), kept", ctx.eax);
    return true;
}

// Movies. The game has two Bink players, both decoding each frame into a YUY2 texture ([player+0x70], two of them):
//   0x0014D870  on its own thread (intro, logos): draws the frame as a screen quad and presents it with
//               D3DDevice_Swap, every frame (0x0014E280)
//   0x0014E060  in the caller's thread (the story cutscenes, called through 0x0014DAA0): enables the Xbox video overlay
//               and sets [player+0xAC], then loops on 0x0014E670 without ever presenting; the overlay hardware scans
//               the frame out by itself
// 0x0014E5A0 shows the decoded frame (player in esi, its texture pair in edi): with [player+0xAC] set through
// D3DDevice_UpdateOverlay, else as the quad. The host has no overlay, so a cutscene left the last game frame on screen
// with its sound playing. Here an overlay frame is drawn as the quad and presented, as the threaded player does.
X86_EXTERN_FUNC(__imp__sub_0014E5A0);
X86_FUNC(sub_0014E5A0) {
    constexpr uint32_t kOverlayFlag = 0xAC;   // byte in the player
    constexpr uint32_t kMovie = 0x0031702C;   // the open Bink movie: width, height first
    constexpr uint32_t kBuildQuad = 0x0014DF70;  // (player in eax, movie in ebx): the 4 vertices at [player]
    constexpr uint32_t kSwap = 0x00260EE0;       // D3DDevice_Swap, flags in eax
    uint32_t player = ctx.esi;
    if (!X86_MEM8(player + kOverlayFlag)) {
        __imp__sub_0014E5A0(ctx, base);
        return;
    }
    if (g_overlays.fetch_add(1, std::memory_order_relaxed) == 0)
        LOGR("dbz: overlay movie %ux%u, drawn as a texture and presented", R32(R32(kMovie)), R32(R32(kMovie) + 4));
    // the overlay player never builds the quad: the movie's size against the screen's, as the threaded player does
    uint32_t eax = ctx.eax, ebx = ctx.ebx;
    ctx.eax = player;
    ctx.ebx = R32(kMovie);
    CallGuest(ctx, kBuildQuad, {});
    ctx.eax = eax;
    ctx.ebx = ebx;
    // the quad path of the same function, then the present the overlay did not need
    X86_MEM8(player + kOverlayFlag) = 0;
    __imp__sub_0014E5A0(ctx, base);
    X86_MEM8(player + kOverlayFlag) = 1;
    eax = ctx.eax;
    ctx.eax = 0;
    CallGuest(ctx, kSwap, {});
    ctx.eax = eax;
}

// 0x002612A0 D3DDevice_UpdateOverlay(surface, src rect in eax, dst rect, color key enable, color key), ret 0x10.
// It programs GPU registers the host device does not have. 0x0014E5A0 above no longer calls it; kept as a no-op.
X86_FUNC(sub_002612A0) {
    (void)base;
    ctx.esp += 4 + 0x10;
}

// Saves. Diagnostics only: each call of the storage device's methods and the XAPI save calls they make, with their
// results and the last error, so the log shows which step of a save fails. The device is the class with the vtable
// at 0x002942C8 (U:\, the hard disk): 1 0x0014CCD0 is the device there, 4 0x0014CD10 room for a save,
// 12 0x0014C8A0 writes one, 18 0x0014C390 reads one.
namespace {

std::atomic<int> g_save_depth{0};  // inside a device method: the XAPI calls below log
std::atomic<int> g_save_lines{0};  // a cap on the lines, a menu can call these every frame
constexpr int kSaveLines = 400;
constexpr uint32_t kGetLastError = 0x001F5DB3;

bool SaveLog() { return g_save_lines.fetch_add(1, std::memory_order_relaxed) < kSaveLines; }

std::string Wide(uint32_t va) {
    std::string s;
    for (uint32_t i = 0; va && i < 128; i++) {
        uint16_t c = R16(va + 2 * i);
        if (!c) break;
        s += c < 0x80 ? char(c) : '?';
    }
    return s;
}

uint32_t LastError(X86Context& ctx) { return CallGuest(ctx, kGetLastError, {}); }

void DeviceCall(X86Context& ctx, uint8_t* base, X86Func* orig, int method) {
    uint32_t self = ctx.ecx, a0 = R32(ctx.esp + 4), a1 = R32(ctx.esp + 8);
    g_save_depth.fetch_add(1);
    orig(ctx, base);
    g_save_depth.fetch_sub(1);
    if (SaveLog())
        LOGR("dbz save: device %08X method %d (%08X, %08X) -> %u, last error %u", self, method, a0, a1, ctx.eax,
             LastError(ctx));
}

}  // namespace

#define DBZ_DEVICE_METHOD(addr, method)                                                 \
    X86_EXTERN_FUNC(__imp__sub_##addr);                                                 \
    X86_FUNC(sub_##addr) { DeviceCall(ctx, base, __imp__sub_##addr, method); }

DBZ_DEVICE_METHOD(0014CCD0, 1)
DBZ_DEVICE_METHOD(0014CD10, 4)
DBZ_DEVICE_METHOD(0014C8A0, 12)
DBZ_DEVICE_METHOD(0014C390, 18)

// XCreateSaveGame(root, name, disposition, flags, path out, size), stdcall
X86_EXTERN_FUNC(__imp__sub_00203A98);
X86_FUNC(sub_00203A98) {
    std::string root = ReadCString(R32(ctx.esp + 4), 64), name = Wide(R32(ctx.esp + 8));
    uint32_t disposition = R32(ctx.esp + 12), out = R32(ctx.esp + 20);
    __imp__sub_00203A98(ctx, base);
    if (SaveLog())
        LOGR("dbz save: XCreateSaveGame(%s, \"%s\", disposition %u) -> %u, path %s", root.c_str(), name.c_str(),
             disposition, ctx.eax, ctx.eax ? "-" : ReadCString(out, 260).c_str());
}

// CreateFileA(name, access, share, security, disposition, flags, template), stdcall: the save's files only
X86_EXTERN_FUNC(__imp__sub_001F76B8);
X86_FUNC(sub_001F76B8) {
    std::string name = ReadCString(R32(ctx.esp + 4), 260);
    uint32_t access = R32(ctx.esp + 8), disposition = R32(ctx.esp + 20);
    bool save = g_save_depth.load() > 0 || (name.size() > 1 && (name[0] == 'U' || name[0] == 'u') && name[1] == ':');
    __imp__sub_001F76B8(ctx, base);
    if (save && SaveLog()) {
        uint32_t h = ctx.eax;
        LOGR("dbz save: CreateFile(%s, access %08X, disposition %u) -> %08X%s", name.c_str(), access, disposition, h,
             h == 0xFFFFFFFF ? ", failed" : "");
        if (h == 0xFFFFFFFF) LOGR("dbz save:   last error %u", LastError(ctx));
    }
}

// GetDiskFreeSpaceExA(root, free to caller, total, free), stdcall.
// The room check (device method 4, 0x0014CD76) compares the blocks a save needs with the LOW dword of the free bytes
// only. The runtime reports 8 GiB free, 0x2'00000000, whose low dword is 0: "not enough free blocks". A real console's
// dashboard counts at most 50,000 blocks of 16 KiB, so the free sizes are capped there and the total below 4 GiB.
X86_EXTERN_FUNC(__imp__sub_001F7E34);
X86_FUNC(sub_001F7E34) {
    constexpr uint64_t kMaxFree = 50000ull * 16384;  // 819,200,000 bytes
    constexpr uint64_t kMaxTotal = 0xF0000000ull;    // 3.75 GiB
    std::string root = ReadCString(R32(ctx.esp + 4), 64);
    uint32_t free = R32(ctx.esp + 8), total = R32(ctx.esp + 12), total_free = R32(ctx.esp + 16);
    __imp__sub_001F7E34(ctx, base);
    if (ctx.eax) {
        if (free && R64(free) > kMaxFree) W64(free, kMaxFree);
        if (total_free && R64(total_free) > kMaxFree) W64(total_free, kMaxFree);
        if (total && R64(total) > kMaxTotal) W64(total, kMaxTotal);
    }
    if (SaveLog())
        LOGR("dbz save: GetDiskFreeSpaceEx(%s) -> %u, free %llu bytes", root.c_str(), ctx.eax,
             free && ctx.eax ? (unsigned long long)R64(free) : 0ull);
}

// GetFileInformationByHandle(file, BY_HANDLE_FILE_INFORMATION out), stdcall. The XAPI asks NtQueryInformationFile for
// FileInternalInformation (class 6), which the runtime does not answer, so every call failed with error 87. Loading a
// save (device method 10) reads the file size from it without checking the result and called every save damaged.
// The same answer from the classes the runtime has: the volume's serial (FileFsVolumeInformation) and the sizes,
// times and attributes (FileNetworkOpenInformation); the file index, the only part of class 6, is the handle.
X86_FUNC(sub_001F753F) {
    (void)base;
    constexpr uint32_t kNtQueryInformationFile = 0x0026FA30;        // import slots
    constexpr uint32_t kNtQueryVolumeInformationFile = 0x0026FA24;
    constexpr uint32_t kSetLastNtError = 0x001F5E09;                // RtlNtStatusToDosError + SetLastError
    uint32_t h = R32(ctx.esp + 4), out = R32(ctx.esp + 8);
    uint32_t iosb = ctx.esp - 0x400, net = iosb + 0x10, vol = iosb + 0x50;  // below anything CallGuest pushes
    auto failed = [](uint32_t status) { return (status & 0xC0000000u) == 0xC0000000u; };
    uint32_t status = CallGuest(ctx, R32(kNtQueryVolumeInformationFile), {h, iosb, vol, 0x18, 1});
    if (!failed(status)) status = CallGuest(ctx, R32(kNtQueryInformationFile), {h, iosb, net, 0x38, 34});
    if (failed(status)) {
        CallGuest(ctx, kSetLastNtError, {status});
        Return(ctx, 0, 2);
        return;
    }
    W32(out + 0x00, R32(net + 0x30));  // attributes
    W64(out + 0x04, R64(net + 0x00));  // creation, last access, last write
    W64(out + 0x0C, R64(net + 0x08));
    W64(out + 0x14, R64(net + 0x10));
    W32(out + 0x1C, R32(vol + 0x08));  // volume serial
    W32(out + 0x20, R32(net + 0x2C));  // size, high then low
    W32(out + 0x24, R32(net + 0x28));
    W32(out + 0x28, 0);                // links, as the XAPI leaves them
    W32(out + 0x2C, 0);                // file index
    W32(out + 0x30, h);
    Return(ctx, 1, 2);
}

// XCalculateSignatureBegin(flags), stdcall
X86_EXTERN_FUNC(__imp__sub_001FAF44);
X86_FUNC(sub_001FAF44) {
    __imp__sub_001FAF44(ctx, base);
    if (g_save_depth.load() > 0 && SaveLog()) LOGR("dbz save: XCalculateSignatureBegin -> %08X", ctx.eax);
}

// WriteFile(file, data, size, written, overlapped), stdcall: failures inside a save
X86_EXTERN_FUNC(__imp__sub_001F7223);
X86_FUNC(sub_001F7223) {
    uint32_t file = R32(ctx.esp + 4), size = R32(ctx.esp + 12), written = R32(ctx.esp + 16);
    __imp__sub_001F7223(ctx, base);
    if (g_save_depth.load() > 0 && (!ctx.eax || (written && R32(written) != size)) && SaveLog())
        LOGR("dbz save: WriteFile(%08X, %u bytes) -> %u, wrote %u, last error %u", file, size, ctx.eax,
             written ? R32(written) : 0, LastError(ctx));
}

// Diagnostics: a release build logs only errors and reports, and the renderer's perf lines are info. Every 10 s:
// frames drawn, task switches, overlay calls, then each guest thread's registers and return addresses (the lines
// read "error:" but are only a snapshot).
void DbzUi(double) { g_frames.fetch_add(1, std::memory_order_relaxed); }

void DbzInit() {
    std::thread([] {
        for (int tick = 1;; tick++) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            size_t tasks;
            {
                std::lock_guard<std::recursive_mutex> lock(g_lock);
                tasks = g_tasks.size();
            }
            LOGR("dbz: %3d s | frames %llu | task switches %llu, contexts %zu | overlay movie frames %llu", tick * 10,
                 (unsigned long long)g_frames.load(), (unsigned long long)g_switches.load(), tasks,
                 (unsigned long long)g_overlays.load());
            for (auto& t : Threads())
                if (t->ctx) {
                    LOGR("dbz:   guest thread %X", t->id);
                    LogGuestStack(*t->ctx, 48);
                }
            logx::Flush();
        }
    }).detach();
}
