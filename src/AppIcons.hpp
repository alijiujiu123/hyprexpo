#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <string>

// The app-icon badge (`label_app_icon`): a workspace card shows the icon of the workspace's
// primary app instead of a number.
//
// The primary app is the workspace's first-opened window while it is still open there; once it
// is closed or moved away, the window with the largest on-screen area stands in. "First opened" is
// tracked from the compositor's window events (a window that lands on a workspace with no other
// window becomes its anchor); windows that already existed when the plugin loaded are seeded by
// their stable id, which grows with every window the compositor creates.
//
// The icon comes from the window's class the way a launcher finds it: the desktop entry whose id,
// StartupWMClass or web-app URL matches the class, then its Icon= through the icon theme (Omarchy's
// current one unless `label_icon_theme` names another), hicolor and /usr/share/pixmaps.
namespace Hyprexpo::AppIcons {
    void                 init();
    void                 shutdown();
    void                 onConfigReload();

    PHLWINDOW            primaryWindow(WORKSPACEID workspace);

    // A square texture of `px` pixels, or null when the class has no icon anywhere. Resolved paths
    // and textures are cached; call from the render thread (it may create a texture).
    SP<Render::ITexture> iconTexture(const std::string& appClass, int px);
}
