#include "rt/irq.hpp"

#include "rt/clock.hpp"
#include "rt/sched.hpp"

namespace rt {

Irq& irq() {
    static Irq i;
    return i;
}

void call_guest(ppc_ctx* c, uint32_t fn, uint32_t a0, uint32_t a1) {
    c->r[3] = a0;
    c->r[4] = a1;
    c->lr = 0;
    ppc_lookup(fn)(c);
}

void Irq::init(ppc_ctx* proto) {
    queue_.clear();
    in_callback_ = false;
    ctx_ = *proto;
    sched().set_idle([this] {
        uint64_t next = next_event_tick();
        if (next == UINT64_MAX && queue_.empty()) return false;
        if (next != UINT64_MAX) clock().advance_to(next);
        run_due_events();
        run_queue();
        return true;
    });
}

bool Irq::enabled() const {
    GuestThread* t = sched().current();
    return !t || t->irq_on;
}

bool Irq::disable() {
    GuestThread* t = sched().current();
    bool prev = t->irq_on;
    t->irq_on = false;
    return prev;
}

void Irq::restore(bool on) {
    GuestThread* t = sched().current();
    bool was = t->irq_on;
    t->irq_on = on;
    if (on && !was) deliver();
}

void Irq::post(std::function<void(ppc_ctx*)> cb) { queue_.push_back(std::move(cb)); }

void Irq::run_queue() {
    if (in_callback_) return;
    in_callback_ = true;
    ++sched().defer_depth;
    // Callbacks run with interrupts disabled; whatever they do to the interrupt state must not
    // leak into the thread they interrupted.
    GuestThread* t = sched().current();
    const bool saved = t ? t->irq_on : true;
    while (!queue_.empty()) {
        auto cb = std::move(queue_.front());
        queue_.pop_front();
        ctx_.r[1] = kStackTop;
        if (t) t->irq_on = false;
        cb(&ctx_);
    }
    if (t) t->irq_on = saved;
    --sched().defer_depth;
    in_callback_ = false;
}

void Irq::deliver() {
    if (in_callback_ || !enabled()) return;
    run_queue();
}

// Strong definition replacing the weak no-op in log.cpp: every HLE entry is a safe point.
void safe_point() {
    if (irq().in_callback()) return;
    // Events must run to completion before any thread they wake gets to run.
    ++sched().defer_depth;
    run_due_events();
    --sched().defer_depth;
    irq().deliver();
    sched().reschedule();
}

}  // namespace rt
