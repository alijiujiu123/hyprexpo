#include "ExpoGesture.hpp"

#include "GestureMomentum.hpp"
#include "HyprlandConfigCompat.hpp"
#include "IOverviewSession.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <cmath>

namespace {
// Config values are read per event, never cached, so `hl.config()`/hyprctl changes apply
// to the next swipe without a plugin reload.
double momentumDecel() {
    return static_cast<double>(std::max<Hyprlang::INT>(0, CompatHyprlandAPI::intValue("plugin:hyprexpo:momentum_decel")));
}

// Accumulated travel (in gesture delta, the unit gesture_distance also uses) a `commit` swipe
// needs before it may switch. Read per event like the momentum keys, so it tunes live.
double commitMinTravel() {
    return static_cast<double>(std::max<Hyprlang::INT>(0, CompatHyprlandAPI::intValue("plugin:hyprexpo:commit_min_travel")));
}

bool momentumDebugEnabled() {
    return CompatHyprlandAPI::intValue("plugin:hyprexpo:momentum_debug") != 0;
}

double momentumWindowSeconds() {
    return static_cast<double>(std::max<Hyprlang::INT>(1, CompatHyprlandAPI::intValue("plugin:hyprexpo:momentum_window_ms"))) / 1000.0;
}
}

namespace {
// The gesture the fingers are on right now (the trackpad owns the object; this only points at it
// while a gesture is open, and the destructor clears it).
CExpoGesture* g_liveExpo = nullptr;

double steadyMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int resampleMs() {
    return std::clamp<int>(static_cast<int>(CompatHyprlandAPI::intValue("plugin:hyprexpo:resample_ms")), 0, 50);
}

// A touchpad event's instant on the steady clock: libinput stamps CLOCK_MONOTONIC ms (truncated to 32
// bits), which is what steady_clock reads on Linux. A stamp not from that clock, or implausibly old,
// falls back to the arrival instant.
double eventMs(const ITrackpadGesture::STrackpadGestureUpdate& e) {
    const double NOW = steadyMs();
    if (!e.swipe)
        return NOW;

    const uint32_t AGE = static_cast<uint32_t>(static_cast<uint64_t>(NOW)) - e.swipe->timeMs;
    return AGE <= 100 ? NOW - AGE : NOW;
}
} // namespace

CExpoGesture::~CExpoGesture() {
    if (g_liveExpo == this)
        g_liveExpo = nullptr;
}

void CExpoGesture::preRender(const PHLMONITOR& monitor) {
    if (!g_liveExpo)
        return;

    g_liveExpo->resampleFrame(monitor);
    g_liveExpo->probeFrame(monitor);
}

void CExpoGesture::probeFrame(const PHLMONITOR& monitor) {
    if (!momentumDebugEnabled() || !monitor || monitor != m_monitor.lock())
        return;

    const double NOW = steadyMs();
    if (NOW == m_lastProbeNow)
        return;

    m_lastProbeNow = NOW;
    m_frames.emplace_back(NOW, m_resampling ? m_sent : static_cast<double>(m_lastDelta));
}

// Each one-period step against the mean of its two neighbours, over consecutive frames one refresh
// apart where the overview moved - how even the drag looked, frame by frame.
std::string CExpoGesture::frameSummary() const {
    const auto   MON    = m_monitor.lock();
    const double PERIOD = MON && MON->m_refreshRate > 1.F ? 1000.0 / MON->m_refreshRate : 1000.0 / 60.0;

    std::vector<double> steps;
    for (size_t i = 1; i < m_frames.size(); i++) {
        const double DT = m_frames[i].first - m_frames[i - 1].first;
        const double DX = m_frames[i].second - m_frames[i - 1].second;
        steps.push_back(DT > 0.0 && DT <= 1.5 * PERIOD && std::abs(DX) > 1e-3 ? DX : NAN);
    }

    double dev = 0.0, sum = 0.0;
    int    n   = 0;
    for (size_t i = 1; i + 1 < steps.size(); i++) {
        if (!std::isfinite(steps[i - 1]) || !std::isfinite(steps[i]) || !std::isfinite(steps[i + 1]))
            continue;
        dev += std::abs(steps[i] - (0.5 * (steps[i - 1] + steps[i + 1])));
        sum += std::abs(steps[i]);
        n++;
    }

    return std::format("frames={} judged={} step_deviation={:.1f}% resample_ms={}", m_frames.size(), n, sum > 0.0 ? dev / sum * 100.0 : 0.0, m_resampling ? resampleMs() : 0);
}

void CExpoGesture::resampleFrame(const PHLMONITOR& monitor) {
    if (!m_resampling || !monitor || monitor != m_monitor.lock())
        return;

    auto* const OV = overview();
    if (!OV || OV->closeCommitted())
        return;

    const double POS = m_resampler.at(steadyMs() - resampleMs());

    if (std::abs(POS - m_sent) > 1e-4) {
        m_sent = POS;
        OV->onSwipeUpdate(std::max(POS, 0.01));
    }

    // Still behind the newest event (the fingers stopped less than resample_ms ago): ask for the
    // frame that catches up - no event will come to schedule it.
    if (std::abs(m_resampler.latest() - m_sent) > 1e-4)
        monitor->scheduleFrame();
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
    m_resampling = resampleMs() > 0;
    m_sent       = 0.0;
    m_frames.clear();
    m_lastProbeNow = -1.0;
    m_resampler.reset(steadyMs(), 0.0);
    g_liveExpo = this;

    // The screen the *pointer* is on.
    //
    // Three rules were tried on 2026-09-22 and this is the one that survived: the focused *window* and
    // the focused *monitor* both go stale with respect to what the user is looking at (measured: a
    // window on workspace 6 reported monitor 0, and a warp to HDMI left the focused monitor on eDP-1),
    // and each of them opened the overview on a screen the user was not looking at — which reads as
    // "three fingers up does nothing". The pointer is where their input actually goes (with
    // `input:follow_mouse = 1` the window focus follows it too, so the pointer is also what gets typed
    // into), and it is what macOS uses for Mission Control.
    const auto monitor = State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
    if (!monitor || !monitor->m_activeWorkspace)
        return;
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
        // `commit` is a decision about an overview that is already open: with none the swipe
        // is inert, so the direction that closes cannot summon one the way `expo` does.
        if (m_action == EExpoGestureAction::Commit)
            return;

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

    if (!m_resampling) {
        OV->onSwipeUpdate(m_lastDelta);
        return;
    }

    // Resampled: record where the fingers are and when; the next frame places the overview
    // (resampleFrame). The 147 Hz touchpad against a 120 Hz panel otherwise moves it 1 or 2 finger
    // steps per frame - the beat edgebounce measured at 45-77 % step unevenness on the same machine.
    m_resampler.add(eventMs(e), m_lastDelta);
    if (const auto MONITOR = m_monitor.lock())
        MONITOR->scheduleFrame();
}

void CExpoGesture::end(const ITrackpadGesture::STrackpadGestureEnd& e) {
    if (momentumDebugEnabled() && !m_frames.empty())
        Log::logger->log(Log::INFO, "HYPREXPO_SWIPE_FRAMES {}", frameSummary());
    const bool WASRESAMPLING = m_resampling;
    m_resampling             = false;
    if (g_liveExpo == this)
        g_liveExpo = nullptr;

    auto* const OV = overview();
    if (!OV || OV->closeCommitted())
        return;

    // The overview decides and lands from its own last position: hand it the fingers' true final
    // travel first (at most resample_ms of motion ahead of what the last frame showed).
    if (WASRESAMPLING && std::abs(m_lastDelta - m_sent) > 1e-4)
        OV->onSwipeUpdate(m_lastDelta);

    const double PROJECTED = e.swipe ? releaseProjectedDelta(e.swipe->timeMs) : -1.0;

    if (momentumDebugEnabled()) {
        const double DISTANCE = static_cast<double>(std::max<Hyprlang::INT>(1, CompatHyprlandAPI::intValue("plugin:hyprexpo:gesture_distance")));
        Log::logger->log(Log::INFO, "HYPREXPO_SWIPE_RELEASE action={} delta={:.2f} velocity={:.1f} projected={:.2f} distance={:.0f} decel={:.0f} window_ms={:.0f}",
                         m_action == EExpoGestureAction::Expo ? "expo" : (m_action == EExpoGestureAction::Cancel ? "cancel" : "commit"), m_lastDelta, m_velocity, PROJECTED, DISTANCE,
                         momentumDecel(),
                         momentumWindowSeconds() * 1000.0);
    }

    // A `commit` swipe below the travel floor is not a selection: re-target it at the workspace
    // the overview opened on, so it closes without switching (what `cancel` does). Without this
    // the release momentum decides alone, and a short brisk brush in the closing direction is
    // enough to land on another workspace.
    const bool SELECTS = m_action != EExpoGestureAction::Cancel && (m_action != EExpoGestureAction::Commit || m_lastDelta >= commitMinTravel());
    if (m_action == EExpoGestureAction::Commit && !SELECTS)
        OV->beginCancelSwipe();

    OV->setClosing(false);
    OV->onSwipeEnd(SELECTS, PROJECTED);
    // onSwipeEnd can tear the overview down, so re-resolve before touching it.
    if (auto* const STILL_ALIVE = overview())
        STILL_ALIVE->resetSwipe();
}
