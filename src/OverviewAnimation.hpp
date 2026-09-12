#pragma once

#include "HyprlandConfigCompat.hpp"

#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>

#include <algorithm>

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

    // Config values are read per animation start, so hl.config()/hyprctl changes apply to
    // the next open or close without a plugin reload.
    inline SP<Hyprutils::Animation::SAnimationPropertyConfig> configForOverview() {
        const auto SPEED = static_cast<float>(std::max<Hyprlang::INT>(0, CompatHyprlandAPI::intValue("plugin:hyprexpo:overview_anim_speed")));

        if (SPEED <= 0.F)
            return Config::animationTree()->getAnimationPropertyConfig("windowsMove");

        static SP<Hyprutils::Animation::SAnimationPropertyConfig> CONFIG;
        if (!CONFIG) {
            CONFIG = makeShared<Hyprutils::Animation::SAnimationPropertyConfig>();
            ::Animation::mgr()->addBezierWithName(OVERVIEWBEZIER, Vector2D{0.25, 0.5}, Vector2D{0.35, 1.0});

            CONFIG->overridden      = true;
            CONFIG->internalBezier  = OVERVIEWBEZIER;
            CONFIG->internalStyle   = "";
            CONFIG->internalEnabled = 1;
            CONFIG->pValues         = CONFIG; // self-reference: getPercent()/enabled() read pValues
        }

        CONFIG->internalSpeed = SPEED;
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
