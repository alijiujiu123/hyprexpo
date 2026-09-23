#pragma once

#include <hyprland/src/managers/input/trackpad/gestures/ITrackpadGesture.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "InputResampler.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class IOverviewSession;

enum class EExpoGestureAction {
    // Opens the overview; with one already open it selects the hovered workspace and switches
    // to it.
    Expo,
    // Closes interactively without selecting anything.
    Cancel,
    // Switches to the hovered workspace on an overview that is already open, and does nothing
    // at all without one -- the closing-direction swipe, which must not summon an overview it
    // is asking to dismiss.
    Commit,
};

class CExpoGesture : public ITrackpadGesture {
  public:
    explicit CExpoGesture(EExpoGestureAction action) : m_action(action) {}
    virtual ~CExpoGesture();

    // Once per frame per monitor, before the renderer decides whether to draw (render.preChecks):
    // with resample_ms on, moves the overview to where the fingers were resample_ms ago.
    static void preRender(const PHLMONITOR& monitor);

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
    // resample_ms: the overview is fed per frame from here instead of per event.
    Hyprexpo::CInputResampler m_resampler;
    bool                      m_resampling = false;
    double                    m_sent       = 0.0; // the last delta handed to the overview
    void                      resampleFrame(const PHLMONITOR& monitor);
    // momentum_debug only: what the overview showed each frame of this gesture (ms, delta), for
    // the release line's step-unevenness figure (the same metric edgebounce's frame probe reports).
    std::vector<std::pair<double, double>> m_frames;
    double                                 m_lastProbeNow = -1.0;
    void                                   probeFrame(const PHLMONITOR& monitor);
    std::string                            frameSummary() const;
};
