#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

/*
 * Zygote seccomp RET_TRAPs some syscalls. After exec the ART handler is
 * gone; Xorg used to treat SIGSYS as fatal. Handle it here: accept→accept4
 * (xtrans already #defines accept to accept4; leftover libc accept() still
 * traps), identity probes return 0, everything else ENOSYS.
 */
#ifdef __ANDROID__

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef SYS_SECCOMP
#define SYS_SECCOMP 1
#endif

#define AARCH64_SVC0 0xd4000001u

void android_install_sigsys_handler(void);

static int
is_identity_nr(long nr)
{
#ifdef SYS_set_robust_list
    if (nr == SYS_set_robust_list)
        return 1;
#endif
#ifdef SYS_set_tid_address
    if (nr == SYS_set_tid_address)
        return 1;
#endif
#ifdef SYS_rseq
    if (nr == SYS_rseq)
        return 1;
#endif
#ifdef SYS_setuid
    if (nr == SYS_setuid || nr == SYS_setgid)
        return 1;
#endif
#ifdef SYS_setresuid
    if (nr == SYS_setresuid || nr == SYS_setresgid)
        return 1;
#endif
#ifdef SYS_capset
    if (nr == SYS_capset)
        return 1;
#endif
    return 0;
}

static void
log_sigsys(long nr, uint64_t pc)
{
    char buf[96];
    int n = 0;
    const char *p = "ArdeskXwl SIGSYS nr=";
    unsigned long v;

    while (*p && n < (int)sizeof(buf) - 1)
        buf[n++] = *p++;
    v = (unsigned long)nr;
    if (v == 0)
        buf[n++] = '0';
    else {
        char tmp[24];
        int t = 0;
        while (v && t < 24) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (t)
            buf[n++] = tmp[--t];
    }
    buf[n++] = '\n';
    (void)write(2, buf, (size_t)n);
    (void)pc;
}

static void
android_sigsys(int signo, siginfo_t *si, void *ucontext)
{
    ucontext_t *uc = ucontext;
#ifdef __aarch64__
    uint64_t *regs;
    uint64_t pc;
    uint32_t insn;
    long nr;
    int skip = 4;
#endif

    (void)signo;
    if (!uc || !si)
        return;
    if (si->si_code != SYS_SECCOMP)
        return;

#ifdef __aarch64__
    regs = uc->uc_mcontext.regs;
    pc = uc->uc_mcontext.pc;
    nr = (long)regs[8];
#ifdef __linux__
    if (si->si_syscall)
        nr = si->si_syscall;
#endif

    log_sigsys(nr, pc);

#ifdef SYS_accept
#ifdef SYS_accept4
    if (nr == SYS_accept) {
        int fd = (int)regs[0];
        void *addr = (void *)(uintptr_t)regs[1];
        socklen_t *len = (socklen_t *)(uintptr_t)regs[2];
        int r = accept4(fd, addr, len, 0);
        regs[0] = (uint64_t)(int64_t)r;
        insn = *(uint32_t *)(uintptr_t)pc;
        if (insn == AARCH64_SVC0)
            uc->uc_mcontext.pc = pc + 4;
        return;
    }
#endif
#endif

    insn = *(uint32_t *)(uintptr_t)pc;
    if (insn != AARCH64_SVC0) {
        uint32_t prev = *(uint32_t *)(uintptr_t)(pc - 4);
        if (prev == AARCH64_SVC0)
            skip = 0;
    }
    if (is_identity_nr(nr))
        regs[0] = 0;
    else
        regs[0] = (uint64_t)(int64_t)-ENOSYS;
    uc->uc_mcontext.pc = pc + (uint64_t)skip;
#endif
}

void
android_install_sigsys_handler(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = android_sigsys;
    act.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&act.sa_mask);
    (void)sigaction(SIGSYS, &act, NULL);
}

__attribute__((constructor(101)))
static void
android_sigsys_ctor(void)
{
    android_install_sigsys_handler();
}

#endif /* __ANDROID__ */
