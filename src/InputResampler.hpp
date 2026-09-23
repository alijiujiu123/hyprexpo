#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

// Input resampling for the three-finger overview drag (a twin of edgebounce's src/InputResampler.hpp,
// which is where it was measured and tested first): where the fingers were at a given instant, interpolated between the
// two touchpad events either side of it.
//
// Why: the touchpad reports at its own rate (147 Hz on the reference machine) and the display
// refreshes at another (120 Hz). Moving the screen once per *event* means ~75 % of frames carry one
// finger step and ~23 % carry two (measured with the frame probe: events/frame 1:84 2:25), so a
// steady finger draws a screen that advances 1, 1, 1, 2, 1, 1, 1, 2 steps - 43-52 % step unevenness,
// a fine judder no frame-rate change can fix. Sampling the finger position *per frame*, a fixed
// latency behind the frame's instant (so the instant is almost always between two real events),
// makes the step proportional to the time between frames, which is what an eye tracking the motion
// expects. The same idea as Android's input resampling and macOS's display-synchronised gestures.
//
// The instant past the newest event is held at the newest event (no extrapolation: a reversal must
// never be overshot); before the oldest, at the oldest. Free of Hyprland headers
// (tests/InputResamplerTests.cpp in edgebounce).
namespace Hyprexpo {

class CInputResampler {
  public:
    void reset(const double tMs, const double position) {
        m_count = 0;
        m_head  = 0;
        add(tMs, position);
    }

    // Samples arrive in time order; an out-of-order or duplicate timestamp is nudged forward so the
    // interpolation never divides by zero.
    void add(double tMs, const double position) {
        if (m_count > 0)
            tMs = std::max(tMs, newest().tMs + 0.01);

        m_samples[m_head] = {tMs, position};
        m_head            = (m_head + 1) % SIZE;
        m_count           = std::min(m_count + 1, SIZE);
    }

    bool empty() const {
        return m_count == 0;
    }

    double latest() const {
        return m_count ? newest().position : 0.0;
    }

    double newestTime() const {
        return m_count ? newest().tMs : 0.0;
    }

    double at(const double tMs) const {
        if (m_count == 0)
            return 0.0;

        if (tMs >= newest().tMs)
            return newest().position;

        if (tMs <= get(0).tMs)
            return get(0).position;

        for (size_t i = m_count - 1; i > 0; i--) {
            const auto& A = get(i - 1);
            const auto& B = get(i);

            if (tMs >= A.tMs)
                return A.position + ((B.position - A.position) * (tMs - A.tMs) / (B.tMs - A.tMs));
        }

        return get(0).position;
    }

  private:
    struct SSample {
        double tMs      = 0.0;
        double position = 0.0;
    };

    static constexpr size_t SIZE = 32; // ~200 ms of touchpad events, far more than any latency used

    const SSample& get(const size_t i) const { // i-th oldest
        return m_samples[(m_head + SIZE - m_count + i) % SIZE];
    }

    const SSample& newest() const {
        return get(m_count - 1);
    }

    std::array<SSample, SIZE> m_samples{};
    size_t                    m_head  = 0;
    size_t                    m_count = 0;
};

} // namespace Hyprexpo
