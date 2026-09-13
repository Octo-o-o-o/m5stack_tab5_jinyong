/*
 * SPDX-FileCopyrightText: 2021 Soar Qin <soarchin@gmail.com>
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Overlay of HeroesOfJinYong src/app/input_repeat.cc for ESP32-P4.
 *
 * Upstream back-fills every missed 20 ms repeat instant: after a long frame
 * (terrain rebuild, submap load, BGM decode) emitRepeats() pushes one move
 * event per 20 ms of elapsed wall time. A 300 ms hitch therefore delivered 15
 * direction events in a single frame and the character teleported 15 cells.
 * That is invisible on a 60 fps desktop and very visible on Tab5.
 *
 * Here a due key emits at most one repeat per drain and the next repeat is
 * rescheduled from *now*, so the walk speed is min(frame rate, 50/s) and a
 * stall costs one step, never a burst.
 *
 * Upstream also auto-repeats the action keys. With InitialDelayMicros = 180 ms
 * that means holding Enter for a fifth of a second -- an ordinary deliberate
 * keypress on a physical keyboard -- delivers a second Accept, and holding it
 * longer delivers one per frame. On chained screens that is a phantom double
 * click: one press on the save menu opened the slot list AND confirmed slot 1,
 * and on the new-game screen it confirmed the name AND answered the attribute
 * prompt. Only the directions repeat here, which is what navigation and
 * walking need; an action key produces exactly one event per physical press.
 *
 * The timings below are local rather than the header's constants: the header is
 * also included by upstream translation units this overlay does not replace, so
 * changing a value there would mean two definitions of the same class.
 * Upstream's 180 ms / 20 ms are desktop numbers and both are wrong here.
 *
 *   - 180 ms is shorter than a deliberate keypress on a physical keyboard, so
 *     one tap on a menu moved the cursor two entries.
 *   - 20 ms is fifty repeats a second. MapWithEvent::handleKeyInput walks one
 *     whole tile per event and throttles nothing of its own, so holding a
 *     direction crossed the map at tens of tiles a second.
 *
 * 400 ms / 100 ms is the ordinary keyboard-driven game feel: a tap is always
 * one step, a deliberate hold walks at about ten tiles a second.
 */

#include "input_repeat.hh"

#include <limits>

namespace hojy::app {

namespace {

/* Longer than any tap that was meant as a single press. */
constexpr std::uint64_t kInitialDelayMicros = 400000;
/* About ten steps a second, capped further by the frame rate. */
constexpr std::uint64_t kRepeatIntervalMicros = 100000;

/* Held-down navigation is a feature; a held-down action key is not. */
bool autoRepeats(InputAction action) {
    switch (action) {
    case InputAction::Up:
    case InputAction::Down:
    case InputAction::Left:
    case InputAction::Right:
        return true;
    default:
        return false;
    }
}

}

void InputRepeater::press(int physicalId, InputDevice device, InputAction action,
                          std::uint64_t timestamp) {
    if (autoRepeats(action)) {
        states_[physicalId] = State{device, action,
                                    timestamp > std::numeric_limits<std::uint64_t>::max() - kInitialDelayMicros
                                        ? std::numeric_limits<std::uint64_t>::max()
                                        : timestamp + kInitialDelayMicros};
    } else {
        /* The same physical id may have carried a repeating action before it
         * was remapped; do not leave that state behind. */
        states_.erase(physicalId);
    }
    queue_.push(InputEvent{timestamp, device, action});
}

void InputRepeater::release(int physicalId) {
    states_.erase(physicalId);
}

void InputRepeater::emitRepeats(std::uint64_t timestamp) {
    for (auto &entry : states_) {
        auto &state = entry.second;
        if (state.nextRepeat > timestamp) { continue; }
        queue_.push(InputEvent{state.nextRepeat, state.device, state.action});
        /* Reschedule from now, not from the missed slot: never catch up. */
        state.nextRepeat = timestamp > std::numeric_limits<std::uint64_t>::max() - kRepeatIntervalMicros
            ? std::numeric_limits<std::uint64_t>::max()
            : timestamp + kRepeatIntervalMicros;
    }
}

std::vector<InputEvent> InputRepeater::drainThrough(std::uint64_t timestamp) {
    emitRepeats(timestamp);
    return queue_.drainThrough(timestamp);
}

}
