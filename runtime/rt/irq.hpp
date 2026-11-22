// Guest "interrupts": callbacks queued by the runtime and delivered at safe points, never nested,
// never while the current thread has interrupts disabled. Callbacks run on a dedicated context.
#pragma once
#include <deque>
#include <functional>

#include "rt/guest.hpp"

namespace rt {

class Irq {
public:
    static constexpr uint32_t kStackTop = 0x81800000u - 16;

    // Copies mem/r2/r13/GQRs from proto for the interrupt context; installs the scheduler idle step.
    void init(ppc_ctx* proto);
    bool enabled() const;
    bool disable();          // returns previous state
    void restore(bool on);   // re-enabling delivers pending callbacks
    void post(std::function<void(ppc_ctx*)> cb);
    void deliver();          // runs queued callbacks if allowed
    bool in_callback() const { return in_callback_; }
    bool pending() const { return !queue_.empty(); }

private:
    void run_queue();
    std::deque<std::function<void(ppc_ctx*)>> queue_;
    ppc_ctx ctx_{};
    bool in_callback_ = false;
};

Irq& irq();

// Calls a guest function at `fn` on the interrupt context with up to two arguments.
void call_guest(ppc_ctx* c, uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0);

}  // namespace rt
