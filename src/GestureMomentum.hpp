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

// The ends of a swipe, with give instead of a wall.
//
// The gesture's travel that means "fully open" is `plugin:hyprexpo:gesture_distance`; past it the
// fraction used to be clamped flat, so pulling further froze the grid — nothing moved, which reads as
// the gesture having stopped rather than as having arrived. This is the same curve the machine already
// uses at a workspace edge (`hypr-edgebounce`'s `RubberBand.hpp`: `t*c*A/(A + c*t)`, progressive
// resistance with an asymptote), so a pull past an end feels like the same material as the bounce.
//
// `fraction` is the raw travel divided by that distance: 0 at one end, 1 at the other, and anything
// outside that past it. `share` is how much of the travel the asymptote is worth (a quarter is plenty
// for a grid that is already fully open), `gain` how quickly it gets there.
inline double resistPastEnds(double fraction, double share = 0.25, double gain = 1.0) {
    const double OVER = fraction < 0.0 ? -fraction : (fraction > 1.0 ? fraction - 1.0 : 0.0);

    if (OVER == 0.0)
        return fraction;

    const double COMPRESSED = (OVER * gain * share) / (share + (gain * OVER));

    return fraction < 0.0 ? -COMPRESSED : 1.0 + COMPRESSED;
}


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
