#pragma once

#include "HyprlandConfigCompat.hpp"

#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>

#include <algorithm>
#include <string>

// The overview's open/close animation is Hyprland's `windowsMove` leaf by default — the
// same leaf the compositor uses when a window changes position, so slowing the overview
// down also slowed window moves. `plugin:hyprexpo:overview_anim_speed` gives the overview
// its own animation config instead (duration = 100 × speed ms, easing = an ease-out like
// the workspace landing curve); 0 keeps the old behaviour of inheriting `windowsMove`.
//
// The config is owned here rather than in the animation tree, because the tree only exposes
// `setConfigForNode` for nodes that already exist — borrowing one of those would put the
// overview back into a shared leaf. Hyprutils' SAnimationPropertyConfig is a plain struct,
// and animated variables take a shared pointer, so a plugin-owned instance is enough
// (it needs a static lifetime, hence the static).
namespace Hyprexpo::Animation {
    inline constexpr const char* OVERVIEWBEZIER = "hyprexpoSettle";

    // The curve every move here rides: the machine's own settle curve when the config defines one,
    // and this plugin's copy of the same four numbers when it does not. `momentumSettle` is the kit's
    // (`looknfeel.lua`; `Ui.motion.curve` and `MotionMath.BEZIER` carry the same numbers, cross-checked
    // by its status.sh), so naming it means the overview, the workspace landing and every panel are on
    // one curve with no copy to drift — and the plugin still works on a machine that has none of that.
    // Checked per animation start: a config reload clears the manager's beziers and re-adds only the
    // config's, so the fallback is restored here too.
    inline std::string settleBezierName() {
        if (::Animation::mgr()->bezierExists("momentumSettle"))
            return "momentumSettle";

        if (!::Animation::mgr()->bezierExists(OVERVIEWBEZIER))
            ::Animation::mgr()->addBezierWithName(OVERVIEWBEZIER, Vector2D{0.25, 0.5}, Vector2D{0.35, 1.0});

        return OVERVIEWBEZIER;
    }

    // One plugin-owned config per shape of motion: an animated variable keeps a *weak* pointer to its
    // config (hence the static lifetime), and the values are read per tick, so two motions with
    // different durations must not share one object.
    inline SP<Hyprutils::Animation::SAnimationPropertyConfig> makePluginConfig() {
        auto CONFIG = makeShared<Hyprutils::Animation::SAnimationPropertyConfig>();

        CONFIG->overridden      = true;
        CONFIG->internalStyle   = "";
        CONFIG->internalEnabled = 1;
        CONFIG->pValues         = CONFIG; // self-reference: getPercent()/enabled() read pValues

        return CONFIG;
    }

    // Config values are read per animation start, so hl.config()/hyprctl changes apply to
    // the next open or close without a plugin reload.
    inline SP<Hyprutils::Animation::SAnimationPropertyConfig> configForOverview() {
        const auto SPEED = static_cast<float>(std::max<Hyprlang::INT>(0, CompatHyprlandAPI::intValue("plugin:hyprexpo:overview_anim_speed")));

        if (SPEED <= 0.F)
            return Config::animationTree()->getAnimationPropertyConfig("windowsMove");

        static SP<Hyprutils::Animation::SAnimationPropertyConfig> CONFIG = makePluginConfig();

        CONFIG->internalBezier = settleBezierName();
        CONFIG->internalSpeed  = SPEED;
        return CONFIG;
    }

    // A card sliding one slot over while a badge drag is open, and settling into its new slot after the
    // drop (`plugin:hyprexpo:card_reorder_ms`, 0 = the cards jump). The camera's curve, its own
    // duration: a one-slot shuffle is not a zoom, and — the reason it is a plain config key rather than
    // a share of `overview_anim_speed` — 0 is what the kit's reduced-motion toggle writes, because a
    // plugin-owned config is not an animation-tree leaf that toggle could otherwise reach.
    inline SP<Hyprutils::Animation::SAnimationPropertyConfig> configForCardReorder() {
        const auto MS = static_cast<float>(std::clamp<Hyprlang::INT>(0, CompatHyprlandAPI::intValue("plugin:hyprexpo:card_reorder_ms"), 2000));

        static SP<Hyprutils::Animation::SAnimationPropertyConfig> CONFIG = makePluginConfig();

        CONFIG->internalBezier = settleBezierName();
        CONFIG->internalSpeed  = MS / 100.F; // duration = 100 × speed ms
        return CONFIG;
    }

    // Call right before an animated assignment (`*var = …`) to give that transition the
    // overview's own curve. The swipe drag itself uses setValueAndWarp, so it is unaffected.
    // PHLANIMVAR is a unique pointer, hence the raw-pointer parameter (`applyTo(var.get())`).
    inline void applyTo(Hyprutils::Animation::CBaseAnimatedVariable* var) {
        if (var)
            var->setConfig(configForOverview());
    }
}
