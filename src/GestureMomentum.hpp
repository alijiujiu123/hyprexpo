#pragma once

#include <algorithm>
#include <cmath>

// Release-momentum maths for the swipe gestures, kept free of Hyprland headers so the
// logic tests can exercise it directly.
//
// Hyprland's own workspace swipe decides with the *mean* per-event displacement over the
// whole gesture (UnifiedWorkspaceSwipeGesture::m_avgSpeed), which lags: a slow drag that
// ends in a flick averages out, and a flick that comes to a rest before release still
// reads as fast. Both gestures here instead carry a recency-weighted velocity and, on
// release, project where the finger would come to rest if it decelerated — that projected
// position is what the overview commit threshold is evaluated against.
namespace Hyprexpo::Momentum {

// Exponential velocity weighting: `tau` is the time constant, so the estimate tracks the
// last ~tau seconds of motion rather than the whole gesture. Elapsed-time weighting keeps
// it event-rate independent.
inline double advanceVelocity(double previous, double delta, double dt, double tau) {
    if (!std::isfinite(dt) || dt <= 0.0)
        return previous;

    const double SAMPLE = delta / dt;

    if (!std::isfinite(tau) || tau <= 0.0)
        return SAMPLE;

    const double K = 1.0 - std::exp(-dt / tau);

    return previous + ((SAMPLE - previous) * K);
}

// Constant-deceleration kinematics: a release carrying `velocity` coasts
// v^2 / (2a) further before stopping, in the direction it was moving.
// The result is clamped at 0 because the gesture delta starts there.
inline double projectDelta(double cumulative, double velocity, double deceleration) {
    if (!std::isfinite(cumulative))
        return 0.0;

    if (!std::isfinite(velocity) || velocity == 0.0 || !std::isfinite(deceleration) || deceleration <= 0.0)
        return std::max(0.0, cumulative);

    const double TRAVEL = (velocity * velocity) / (2.0 * deceleration);

    return std::max(0.0, cumulative + std::copysign(TRAVEL, velocity));
}

}
