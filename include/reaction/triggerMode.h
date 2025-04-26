/*
 * Copyright (c) 2025 Lummy
 *
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full details.
 */

#ifndef REACTION_TRIGGERMODE_H
#define REACTION_TRIGGERMODE_H

#include <functional>

namespace reaction {

// Creates a functor to get the value from the function and arguments
template <typename F, typename... A>
inline auto createGetFunRef(F &&f, A &&...args) {
    return [f = std::forward<F>(f), &...args = *args]() mutable {
        return std::invoke(f, args.get()...); // Invokes function f with arguments' values
    };
}

// Trigger mode that always returns true
struct AlwaysTrigger {
    // Always triggers regardless of the arguments
    bool checkTrigger() {
        return true; // Always triggers
    }
};

// Trigger mode that triggers based on value change
struct ChangedTrigger {
    // Only triggers if the provided boolean argument is true
    bool checkTrigger() {
        return m_changed; // All arguments must be true for the trigger to occur
    }

    void setChanged(bool changed) {
        m_changed = changed;
    }

private:
    bool m_changed = true;
};

// Trigger mode based on a threshold
struct ThresholdTrigger {
    // Set the threshold function to determine whether to trigger based on arguments
    template <typename F, ArgNonEmptyCC... A>
    void setThreshold(F &&f, A &&...args) {
        m_thresholdFun = createGetFunRef(std::forward<F>(f), std::forward<A>(args)...);
    }

    // Check whether the trigger condition is met using the threshold function
    bool checkTrigger() {
        return std::invoke(m_thresholdFun); // Calls the threshold function to determine trigger condition
    }

private:
    // Function to evaluate whether the threshold condition is met
    std::function<bool()> m_thresholdFun = []() { return true; }; // Default threshold function always returns true
};

} // namespace reaction

#endif // REACTION_TRIGGERMODE_H
