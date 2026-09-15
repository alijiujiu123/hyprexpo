#define WLR_USE_UNSTABLE

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/event/EventBus.hpp>

#include "Dispatchers.hpp"
#include "globals.hpp"
#include "IOverviewSession.hpp"
#include "OverviewCapture.hpp"
#include "OverviewInternal.hpp"
#include "PluginConfig.hpp"
#include <stdexcept>
#include <string>

#ifndef HYPREXPO_VERSION
#define HYPREXPO_VERSION "v0.0.0-dev"
#endif

// Methods
inline CFunctionHook* g_pRenderWorkspaceHook = nullptr;
inline CFunctionHook* g_pAddDamageHookA      = nullptr;
inline CFunctionHook* g_pAddDamageHookB      = nullptr;
inline CFunctionHook* g_pCommitWindowHook    = nullptr;
typedef void (*origRenderWorkspace)(void*, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&, const CBox&);
typedef void (*origAddDamageA)(void*, const CBox&);
typedef void (*origAddDamageB)(void*, const pixman_region32_t*);
typedef void (*origCommitWindow)(void*);

// Hyprland only renders the visible workspace, so a client on a hidden one gets no frame
// callbacks and commits only when it has something new to show. Its commits carry the
// damage it produced (`SSurfaceState::updateFrom` clears the damage of a commit that has
// none), which makes this the one place where "this workspace is alive" is observable
// without rendering it. Anything that is merely a no-op commit (frame callback requests,
// early sync) leaves the damage empty and is ignored.
static void hkCommitWindow(void* thisptr) {
    ((origCommitWindow)(g_pCommitWindowHook->m_original))(thisptr);

    if (g_overviews.empty())
        return;

    const auto WINDOW = (Desktop::View::CWindow*)thisptr;
    if (!WINDOW->m_isMapped || !WINDOW->m_workspace || WINDOW->m_workspace->m_visible || WINDOW->isHidden())
        return;

    const auto SURFACE = WINDOW->wlSurface();
    if (!SURFACE)
        return;

    const auto RESOURCE = SURFACE->resource();
    if (!RESOURCE)
        return;

    const auto& CURRENT = RESOURCE->m_current;
    if (CURRENT.damage.empty() && CURRENT.bufferDamage.empty())
        return;

    markWorkspaceContentDirty(WINDOW->m_workspace);
}

static void hkRenderWorkspace(void* thisptr, PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp& now, const CBox& geometry) {
    auto* const OV = overviewForMonitor(pMonitor);

    if (!OV || isRenderingOverview() || OV->blocksOverviewRendering() || !OV->shouldRenderOverviewForMonitor(pMonitor))
        ((origRenderWorkspace)(g_pRenderWorkspaceHook->m_original))(thisptr, pMonitor, pWorkspace, now, geometry);
    else
        OV->render();
}

static void hkAddDamageA(void* thisptr, const CBox& box) {
    const auto PMONITOR   = (Monitor::CMonitor*)thisptr;
    const auto PMONITORSP = PMONITOR ? PMONITOR->m_self.lock() : PHLMONITOR{};

    auto* const OV = overviewForMonitor(PMONITORSP);

    if (!OV || !OV->shouldRenderOverviewForMonitor(PMONITORSP) || OV->blocksDamageReporting()) {
        ((origAddDamageA)g_pAddDamageHookA->m_original)(thisptr, box);
        return;
    }

    OV->onDamageReported();
}

static void hkAddDamageB(void* thisptr, const pixman_region32_t* rg) {
    const auto PMONITOR   = (Monitor::CMonitor*)thisptr;
    const auto PMONITORSP = PMONITOR ? PMONITOR->m_self.lock() : PHLMONITOR{};

    auto* const OV = overviewForMonitor(PMONITORSP);

    if (!OV || !OV->shouldRenderOverviewForMonitor(PMONITORSP) || OV->blocksDamageReporting()) {
        ((origAddDamageB)g_pAddDamageHookB->m_original)(thisptr, rg);
        return;
    }

    OV->onDamageReported();
}

static void failNotif(const std::string& reason) {
    HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Failure in initialization: " + reason, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    if (HASH != __hyprland_api_get_client_hash()) {
        failNotif("Version mismatch (headers ver is not equal to running hyprland ver)");
        throw std::runtime_error("[he] Version mismatch");
    }

    auto FNS = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWorkspace");
    if (FNS.empty()) {
        failNotif("no fns for hook renderWorkspace");
        throw std::runtime_error("[he] No fns for hook renderWorkspace");
    }

    g_pRenderWorkspaceHook = HyprlandAPI::createFunctionHook(PHANDLE, FNS[0].address, (void*)hkRenderWorkspace);

    FNS = HyprlandAPI::findFunctionsByName(PHANDLE, "addDamageEPK15pixman_region32");
    if (FNS.empty()) {
        failNotif("no fns for hook addDamageEPK15pixman_region32");
        throw std::runtime_error("[he] No fns for hook addDamageEPK15pixman_region32");
    }

    g_pAddDamageHookB = HyprlandAPI::createFunctionHook(PHANDLE, FNS[0].address, (void*)hkAddDamageB);

    FNS = HyprlandAPI::findFunctionsByName(PHANDLE, "_ZN7Monitor8CMonitor9addDamageERKN9Hyprutils4Math4CBoxE");
    if (FNS.empty()) {
        failNotif("no fns for hook Monitor::CMonitor::addDamage(CBox)");
        throw std::runtime_error("[he] No fns for hook Monitor::CMonitor::addDamage(CBox)");
    }

    g_pAddDamageHookA = HyprlandAPI::createFunctionHook(PHANDLE, FNS[0].address, (void*)hkAddDamageA);

    bool success = g_pRenderWorkspaceHook->hook();
    success      = success && g_pAddDamageHookA->hook();
    success      = success && g_pAddDamageHookB->hook();

    // Dirty tile refresh is an optimisation, not a requirement: an older or patched
    // compositor without this symbol keeps the previous behaviour instead of failing to
    // load the plugin.
    //
    // While loaded, this hook occupies the symbol: Hyprland allows one hook per function
    // (`CHookSystem::m_activeHooks`, "function is already hooked"), so any other plugin that
    // wants to react to window commits has to hook something else - `CWLSurfaceResource::
    // commitState` is the same event one level down and is free. A second hook on this one
    // fails silently as far as the session is concerned, and this machine runs with the
    // compositor's own log switched off, so the failure is only visible in whichever
    // plugin's own log file reports it.
    FNS = HyprlandAPI::findFunctionsByName(PHANDLE, "_ZN7Desktop4View7CWindow12commitWindowEv");
    if (FNS.empty())
        Log::logger->log(Log::ERR, "[hyprexpo] no fn for hook CWindow::commitWindow, dirty tile refresh disabled");
    else {
        g_pCommitWindowHook = HyprlandAPI::createFunctionHook(PHANDLE, FNS[0].address, (void*)hkCommitWindow);
        success             = success && g_pCommitWindowHook->hook();
    }

    if (!success) {
        failNotif("Failed initializing hooks");
        throw std::runtime_error("[he] Failed initializing hooks");
    }

    static auto P = Event::bus()->m_events.render.pre.listen([](PHLMONITOR pMonitor) {
        if (auto* const OV = overviewForMonitor(pMonitor))
            OV->onPreRender();
    });

    static auto PMONITORREMOVED = Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR monitor) {
        if (auto* const OV = overviewForMonitor(monitor))
            destroyOverview(OV);
    });

    static auto PKEY = Event::bus()->m_events.input.keyboard.key.listen([](IKeyboard::SKeyEvent event, Event::SCallbackInfo& info) {
        if (shouldCancelOverview(event)) {
            info.cancelled = true;
            closeOverviews(false);
            return;
        }

        if (shouldSelectWorkspaceFromKey(event))
            info.cancelled = true;
    });

    static auto PCFG = Event::bus()->m_events.config.reloaded.listen([]() {
        Hyprexpo::Capture::notifyOverviewCaptureConfigReload();
        forEachOverview([](IOverviewSession& overview) { overview.onConfigReload(); });
        syncExpoGestureFromConfig();
    });

    registerHyprexpoDispatchers();

    registerHyprexpoConfigValues();

    HyprlandAPI::reloadConfig();

    syncExpoGestureFromConfig();

    return {"hyprexpo", "hyprexpo+ with keyboard selection, labels, and borders", "sandwich", HYPREXPO_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
    disableExpoGestureRegistration();

    destroyAllOverviews();
    g_pHyprRenderer->m_renderPass.removeAllOfType("COverviewPassElement");

    Config::mgr()->reload();
    resetDispatcherRuntime();
}
