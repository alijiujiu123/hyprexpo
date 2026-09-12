#include "ExpoGesture.hpp"

#include "GestureMomentum.hpp"
#include "HyprlandConfigCompat.hpp"
#include "IOverviewSession.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <algorithm>

namespace {
// Config values are read per event, never cached, so `hl.config()`/hyprctl changes apply
// to the next swipe without a plugin reload.
double momentumDecel() {
    return static_cast<double>(std::max<Hyprlang::INT>(0, CompatHyprlandAPI::intValue("plugin:hyprexpo:momentum_decel")));
}

bool momentumDebugEnabled() {
    return CompatHyprlandAPI::intValue("plugin:hyprexpo:momentum_debug") != 0;
}

double momentumWindowSeconds() {
    return static_cast<double>(std::max<Hyprlang::INT>(1, CompatHyprlandAPI::intValue("plugin:hyprexpo:momentum_window_ms"))) / 1000.0;
}
}

void CExpoGesture::begin(const ITrackpadGesture::STrackpadGestureBegin& e) {
    ITrackpadGesture::begin(e);

    m_lastDelta    = 0.F;
    m_firstUpdate  = true;
    m_velocity     = 0.0;
    m_lastSampleMs = e.swipe ? e.swipe->timeMs : 0;
    m_haveVelocity = false;
    m_monitor.reset();
    m_sessionGeneration = 0;

    const auto monitor = State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
    if (!monitor || !monitor->m_activeWorkspace)
        return;

    m_monitor = monitor;

    auto* const OV = overviewForMonitor(monitor);
    m_sessionGeneration = OV ? OV->sessionGeneration() : 0;
    if (m_action == EExpoGestureAction::Cancel) {
        if (!OV || OV->closeCommitted())
            return;

        OV->beginCancelSwipe();
        return;
    }

    if (!OV) {
        if (auto* const CREATED = createOverview(monitor, true))
            m_sessionGeneration = CREATED->sessionGeneration();
    }
    else if (!OV->closeCommitted()) {
        OV->selectHoveredWorkspace();
        OV->setClosing(true);
    }
}

IOverviewSession* CExpoGesture::overview() const {
    return overviewForSession(overviewMonitorKey(m_monitor.lock()), m_sessionGeneration);
}

void CExpoGesture::trackVelocity(double delta, uint32_t timeMs) {
    if (m_lastSampleMs == 0 || timeMs <= m_lastSampleMs) {
        m_lastSampleMs = timeMs;
        return;
    }

    m_velocity     = Hyprexpo::Momentum::advanceVelocity(m_velocity, delta, (timeMs - m_lastSampleMs) / 1000.0, momentumWindowSeconds());
    m_lastSampleMs = timeMs;
    m_haveVelocity = true;
}

double CExpoGesture::releaseProjectedDelta(uint32_t endTimeMs) const {
    if (!m_haveVelocity)
        return -1.0;

    const double DECEL = momentumDecel();
    if (DECEL <= 0.0)
        return -1.0;

    // The stretch between the last motion event and the release counts as no motion, so a
    // finger that came to a rest before letting go does not commit on stale velocity.
    double VELOCITY = m_velocity;
    if (endTimeMs > m_lastSampleMs)
        VELOCITY = Hyprexpo::Momentum::advanceVelocity(VELOCITY, 0.0, (endTimeMs - m_lastSampleMs) / 1000.0, momentumWindowSeconds());

    return Hyprexpo::Momentum::projectDelta(m_lastDelta, VELOCITY, DECEL);
}

void CExpoGesture::update(const ITrackpadGesture::STrackpadGestureUpdate& e) {
    auto* const OV = overview();
    if (!OV || OV->closeCommitted())
        return;

    if (m_firstUpdate) {
        m_firstUpdate = false;
        return;
    }

    const double DELTA = distance(e);

    if (e.swipe)
        trackVelocity(DELTA, e.swipe->timeMs);

    m_lastDelta += DELTA;

    if (m_lastDelta <= 0.01) // plugin will crash if swipe ends at <= 0
        m_lastDelta = 0.01;

    OV->onSwipeUpdate(m_lastDelta);
}

void CExpoGesture::end(const ITrackpadGesture::STrackpadGestureEnd& e) {
    auto* const OV = overview();
    if (!OV || OV->closeCommitted())
        return;

    const double PROJECTED = e.swipe ? releaseProjectedDelta(e.swipe->timeMs) : -1.0;

    if (momentumDebugEnabled()) {
        const double DISTANCE = static_cast<double>(std::max<Hyprlang::INT>(1, CompatHyprlandAPI::intValue("plugin:hyprexpo:gesture_distance")));
        Log::logger->log(Log::INFO, "HYPREXPO_SWIPE_RELEASE action={} delta={:.2f} velocity={:.1f} projected={:.2f} distance={:.0f} decel={:.0f} window_ms={:.0f}",
                         m_action == EExpoGestureAction::Expo ? "expo" : "cancel", m_lastDelta, m_velocity, PROJECTED, DISTANCE, momentumDecel(),
                         momentumWindowSeconds() * 1000.0);
    }

    OV->setClosing(false);
    OV->onSwipeEnd(m_action == EExpoGestureAction::Expo, PROJECTED);
    // onSwipeEnd can tear the overview down, so re-resolve before touching it.
    if (auto* const STILL_ALIVE = overview())
        STILL_ALIVE->resetSwipe();
}
