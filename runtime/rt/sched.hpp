// Guest threads as host fibers (ucontext) on one host thread, with SDK priority semantics.
#pragma once
#include <ucontext.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "rt/guest.hpp"

namespace rt {

struct GuestThread {
    enum State { Ready, Running, Waiting, Suspended, Moribund };

    uint32_t ea = 0;  // guest OSThread address
    int prio = 16;
    State state = Suspended;
    int suspend = 1;
    bool detached = false;
    bool started = false;
    uint32_t entry = 0;
    uint32_t wait_key = 0;   // queue address being slept on (0 = none)
    uint32_t exit_value = 0;
    uint64_t seq = 0;        // FIFO order among equal priorities
    ppc_ctx* ctx = nullptr;  // points at own_ctx, or the caller's ctx for the main thread
    ppc_ctx own_ctx{};
    ucontext_t uc{};
    void* stack = nullptr;   // mmap'd host stack (with guard page), null for main
    size_t stack_size = 0;
    ~GuestThread();
};

class Scheduler {
public:
    static constexpr size_t kHostStack = 8u << 20;

    void init(ppc_ctx* main_ctx, uint32_t main_thread_ea);

    GuestThread* create(uint32_t thread_ea, uint32_t entry, uint32_t param, uint32_t stack_top, int prio,
                        bool detached);
    int resume(GuestThread* t);  // returns previous suspend count
    void suspend(GuestThread* t);
    void yield();
    void sleep_on(uint32_t queue_ea);
    void wakeup(uint32_t queue_ea);
    [[noreturn]] void exit_current(uint32_t value);
    void cancel(GuestThread* t);
    bool join(GuestThread* t, uint32_t* value);

    GuestThread* current() const { return current_; }
    GuestThread* by_ea(uint32_t ea) const;
    int active_count() const;

    // Switch to the highest-priority ready thread if it outranks the current one
    // (or if the current one can no longer run).
    void reschedule();

    // Called repeatedly while no thread is ready; returns false if nothing can ever become ready.
    void set_idle(std::function<bool()> step) { idle_ = std::move(step); }
    void set_deadlock_handler(std::function<void()> h) { on_deadlock_ = std::move(h); }

    // While > 0 (inside callbacks / idle), wakeups only mark threads ready; preemption waits
    // for the next reschedule().
    int defer_depth = 0;

private:
    friend void fiber_main();
    GuestThread* pick() const;
    void make_ready(GuestThread* t);
    void switch_to(GuestThread* t);
    void write_guest(GuestThread* t);

    std::vector<std::unique_ptr<GuestThread>> threads_;
    std::vector<std::unique_ptr<GuestThread>> graveyard_;  // finished threads whose stacks may still be live
    GuestThread* current_ = nullptr;
    uint64_t next_seq_ = 1;
    std::function<bool()> idle_;
    std::function<void()> on_deadlock_;
};

Scheduler& sched();

}  // namespace rt
