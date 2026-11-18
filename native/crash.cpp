// Crash reporter for the PS4 (after Retrocade's platform/ps4/crash.cpp): on a crash, the fault address,
// the last step the app reached and part of the stack go to /data/PS4Tegelmaker/crash.log, so the place
// can be found in native/out/app.elf.
//
// sigaction comes from libkernel and wants FreeBSD's struct sigaction, not musl's: <signal.h> is
// deliberately not included here, and the FreeBSD signal numbers are used.
#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" const char *ef_step = "start";  // set by main.cpp at each stage
extern "C" int ef_last_rect[4] = {0, 0, 0, 0};  // set by ui::rect

namespace {
struct FbsdSigaction {
    void (*handler)(int, void *, void *);
    int flags;
    uint32_t mask[4];
};
constexpr int kSaSiginfo = 0x40;
constexpr int kSignals[] = {4 /*ILL*/, 6 /*ABRT*/, 8 /*FPE*/, 10 /*BUS*/, 11 /*SEGV*/};
}  // namespace

extern "C" int sigaction(int, const FbsdSigaction *, FbsdSigaction *);

// A fixed point in the code: its address here minus its address in the ELF gives the load address.
extern "C" __attribute__((noinline, used)) void ef_crash_anchor() { __asm__ volatile(""); }

static void on_crash(int sig, void *info, void *context)
{
    auto *uc = static_cast<unsigned char *>(context);
    uint64_t rip = 0, rsp = 0, rbp = 0, addr = 0;
    if (uc) {  // FreeBSD amd64: ucontext = sigmask (16 bytes) + mcontext
        std::memcpy(&rip, uc + 16 + 160, 8);
        std::memcpy(&rsp, uc + 16 + 184, 8);
        std::memcpy(&rbp, uc + 16 + 72, 8);
    }
    if (info)
        std::memcpy(&addr, static_cast<unsigned char *>(info) + 24, 8);  // si_addr
    int fd = open("/data/PS4Tegelmaker/crash.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    char buf[320];
    int n = std::snprintf(buf, sizeof buf, "CRASH signaal %d stap \"%s\" adres 0x%llx rip 0x%llx rsp 0x%llx rbp 0x%llx anker 0x%llx\n",
                          sig, ef_step ? ef_step : "?", (unsigned long long)addr, (unsigned long long)rip,
                          (unsigned long long)rsp, (unsigned long long)rbp, (unsigned long long)(uintptr_t)&ef_crash_anchor);
    if (fd >= 0)
        (void)!write(fd, buf, n);
    n = std::snprintf(buf, sizeof buf, "laatste rechthoek: x %d y %d b %d h %d\n", ef_last_rect[0], ef_last_rect[1],
                      ef_last_rect[2], ef_last_rect[3]);
    if (fd >= 0)
        (void)!write(fd, buf, n);
    if (fd >= 0 && rsp) {
        const uint64_t *sp = reinterpret_cast<const uint64_t *>(rsp);
        for (int i = 0; i < 160; i += 4) {
            n = std::snprintf(buf, sizeof buf, "stack+%03x %016llx %016llx %016llx %016llx\n", i * 8,
                              (unsigned long long)sp[i], (unsigned long long)sp[i + 1], (unsigned long long)sp[i + 2],
                              (unsigned long long)sp[i + 3]);
            (void)!write(fd, buf, n);
        }
    }
    if (fd >= 0)
        close(fd);
    FbsdSigaction standard{};  // back to the default: the system handles the crash as always
    sigaction(sig, &standard, nullptr);
}

extern "C" void ef_install_crash_handler()
{
    FbsdSigaction sa{};
    sa.handler = on_crash;
    sa.flags = kSaSiginfo;
    for (int s : kSignals)
        sigaction(s, &sa, nullptr);
}
