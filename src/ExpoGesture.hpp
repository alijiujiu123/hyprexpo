#pragma once

#include <hyprland/src/managers/input/trackpad/gestures/ITrackpadGesture.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <cstdint>

class IOverviewSession;

enum class EExpoGestureAction {
    Expo,
    Cancel,
};

class CExpoGesture : public ITrackpadGesture {
  public:
    explicit CExpoGesture(EExpoGestureAction action) : m_action(action) {}
    virtual ~CExpoGesture() = default;

    virtual void begin(const ITrackpadGesture::STrackpadGestureBegin& e);
    virtual void update(const ITrackpadGesture::STrackpadGestureUpdate& e);
    virtual void end(const ITrackpadGesture::STrackpadGestureEnd& e);

  private:
    IOverviewSession*    overview() const;
    // Track the release velocity from the per-event libinput timestamps: the compositor
    // only ever sees positions, so the gesture is the only place that can know how fast
    // the finger was moving when it let go.
    void                 trackVelocity(double delta, uint32_t timeMs);
    // Cumulative delta the release velocity would still cover (negative = unknown).
    double               releaseProjectedDelta(uint32_t endTimeMs) const;

    // Monitor the gesture started on, so update/end keep driving the same
    // overview even when other monitors have one open too.
    PHLMONITORREF m_monitor;
    uint64_t m_sessionGeneration = 0;
    const EExpoGestureAction m_action;
    float                    m_lastDelta   = 0.F;
    bool                     m_firstUpdate = false;
    double                   m_velocity      = 0.0;
    uint32_t                 m_lastSampleMs  = 0;
    bool                     m_haveVelocity  = false;
};
