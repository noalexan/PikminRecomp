#include "rt/sched.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include "rt/log.hpp"

namespace rt {

// Guest OSThread field offsets (SDK OSThread layout).
namespace {
constexpr uint32_t kState = 0x2C8, kAttr = 0x2CA, kSuspend = 0x2CC, kPrio = 0x2D0, kBase = 0x2D4, kVal = 0x2D8;
constexpr uint16_t kOsReady = 1, kOsRunning = 2, kOsWaiting = 4, kOsMoribund = 8;
}  // namespace

GuestThread::~GuestThread() {
    if (stack) munmap(stack, stack_size);
}

Scheduler& sched() {
    static Scheduler s;
    return s;
}

void Scheduler::init(ppc_ctx* main_ctx, uint32_t main_thread_ea) {
    for (auto& t : threads_) graveyard_.push_back(std::move(t));
    threads_.clear();
    graveyard_.clear();  // only called from the main host context, so no stack is live
    auto main = std::make_unique<GuestThread>();
    main->ea = main_thread_ea;
    main->prio = 16;
    main->state = GuestThread::Running;
    main->suspend = 0;
    main->started = true;
    main->ctx = main_ctx;
    current_ = main.get();
    threads_.push_back(std::move(main));
    next_seq_ = 1;
    defer_depth = 0;
    idle_ = nullptr;
    on_deadlock_ = nullptr;
    write_guest(current_);
}

void fiber_main() {
    Scheduler& s = sched();
    GuestThread* t = s.current_;
    ppc_lookup(t->entry)(t->ctx);
    s.exit_current(t->ctx->r[3]);
}

GuestThread* Scheduler::create(uint32_t thread_ea, uint32_t entry, uint32_t param, uint32_t stack_top, int prio,
                               bool detached) {
    auto t = std::make_unique<GuestThread>();
    t->ea = thread_ea;
    t->prio = prio;
    t->detached = detached;
    t->entry = entry;
    t->own_ctx = *threads_.front()->ctx;  // inherit mem, lc, r2, r13, GQRs from main
    t->ctx = &t->own_ctx;
    t->ctx->r[1] = (stack_top - 8) & ~7u;
    t->ctx->r[3] = param;
    t->ctx->lr = 0;
    wr32(t->ctx, t->ctx->r[1], 0);  // back chain terminator

    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    t->stack_size = kHostStack + page;
    t->stack = mmap(nullptr, t->stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (t->stack == MAP_FAILED) {
        std::perror("mmap thread stack");
        std::abort();
    }
    mprotect(t->stack, page, PROT_NONE);  // guard page below the stack
    getcontext(&t->uc);
    t->uc.uc_stack.ss_sp = t->stack;
    t->uc.uc_stack.ss_size = t->stack_size;
    t->uc.uc_link = nullptr;
    makecontext(&t->uc, fiber_main, 0);

    GuestThread* raw = t.get();
    threads_.push_back(std::move(t));
    write_guest(raw);
    return raw;
}

GuestThread* Scheduler::by_ea(uint32_t ea) const {
    for (auto& t : threads_)
        if (t->ea == ea) return t.get();
    return nullptr;
}

int Scheduler::active_count() const {
    int n = 0;
    for (auto& t : threads_)
        if (t->state != GuestThread::Moribund) ++n;
    return n;
}

void Scheduler::write_guest(GuestThread* t) {
    if (!t->ea || !current_) return;
    ppc_ctx* c = threads_.front()->ctx;
    uint16_t st = t->state == GuestThread::Running    ? kOsRunning
                  : t->state == GuestThread::Ready    ? kOsReady
                  : t->state == GuestThread::Moribund ? kOsMoribund
                  : t->state == GuestThread::Waiting  ? kOsWaiting
                                                      : 0;
    wr16(c, t->ea + kState, st);
    wr16(c, t->ea + kAttr, t->detached ? 1 : 0);
    wr32(c, t->ea + kSuspend, static_cast<uint32_t>(t->suspend));
    wr32(c, t->ea + kPrio, static_cast<uint32_t>(t->prio));
    wr32(c, t->ea + kBase, static_cast<uint32_t>(t->prio));
    wr32(c, t->ea + kVal, t->exit_value);
}

void Scheduler::make_ready(GuestThread* t) {
    t->state = GuestThread::Ready;
    t->wait_key = 0;
    t->seq = next_seq_++;
    write_guest(t);
}

GuestThread* Scheduler::pick() const {
    GuestThread* best = nullptr;
    for (auto& up : threads_) {
        GuestThread* t = up.get();
        if (t->state != GuestThread::Ready && t->state != GuestThread::Running) continue;
        if (!best || t->prio < best->prio || (t->prio == best->prio && t->seq < best->seq)) best = t;
    }
    return best;
}

void Scheduler::switch_to(GuestThread* t) {
    GuestThread* old = current_;
    if (t == old) {
        t->state = GuestThread::Running;
        return;
    }
    if (old->state == GuestThread::Running) {
        old->state = GuestThread::Ready;
        write_guest(old);
    }
    t->state = GuestThread::Running;
    t->started = true;
    current_ = t;
    write_guest(t);
    swapcontext(&old->uc, &t->uc);
    // Resumed: free stacks of threads that finished while we were away.
    if (current_ == old) graveyard_.clear();
}

void Scheduler::reschedule() {
    if (defer_depth > 0) return;
    for (;;) {
        GuestThread* best = pick();
        if (best) {
            bool cur_runnable = current_->state == GuestThread::Running;
            if (!cur_runnable || best->prio < current_->prio || best == current_ ||
                (best->prio == current_->prio && best->seq < current_->seq)) {
                switch_to(best);
            }
            return;
        }
        // Nothing can run: let time pass / deliver events.
        ++defer_depth;
        bool progress = idle_ && idle_();
        --defer_depth;
        if (!progress) {
            log("sched", "deadlock: all threads waiting");
            if (on_deadlock_) on_deadlock_();
            std::exit(4);
        }
    }
}

int Scheduler::resume(GuestThread* t) {
    int prev = t->suspend;
    if (t->suspend > 0 && --t->suspend == 0 && t->state == GuestThread::Suspended) {
        make_ready(t);
        reschedule();
    } else {
        write_guest(t);
    }
    return prev;
}

void Scheduler::suspend(GuestThread* t) {
    ++t->suspend;
    if (t->state == GuestThread::Ready || t->state == GuestThread::Running) {
        t->state = GuestThread::Suspended;
        write_guest(t);
        if (t == current_) reschedule();
    }
}

void Scheduler::yield() {
    current_->seq = next_seq_++;
    GuestThread* best = pick();
    if (best && best != current_ && best->prio <= current_->prio) switch_to(best);
}

void Scheduler::sleep_on(uint32_t queue_ea) {
    current_->state = GuestThread::Waiting;
    current_->wait_key = queue_ea;
    write_guest(current_);
    reschedule();
}

void Scheduler::wakeup(uint32_t queue_ea) {
    for (auto& t : threads_)
        if (t->state == GuestThread::Waiting && t->wait_key == queue_ea) make_ready(t.get());
    reschedule();
}

void Scheduler::exit_current(uint32_t value) {
    GuestThread* t = current_;
    t->exit_value = value;
    t->state = GuestThread::Moribund;
    write_guest(t);
    wakeup(t->ea + 1);  // join waiters sleep on ea+1 (queue addresses are 4-aligned)
    reschedule();
    std::fprintf(stderr, "exit_current: moribund thread resumed\n");
    std::abort();
}

void Scheduler::cancel(GuestThread* t) {
    if (t == current_) exit_current(0);
    t->state = GuestThread::Moribund;
    t->wait_key = 0;
    write_guest(t);
    wakeup(t->ea + 1);
}

bool Scheduler::join(GuestThread* t, uint32_t* value) {
    if (!t || t == current_ || t->detached) return false;
    while (t->state != GuestThread::Moribund) sleep_on(t->ea + 1);
    if (value) *value = t->exit_value;
    return true;
}

}  // namespace rt
