#include "AppIcons.hpp"

#include "globals.hpp"
#include "HyprlandConfigCompat.hpp"
#include "HyprexpoLogic.hpp"
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprgraphics/image/Image.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Hyprexpo::AppIcons {

namespace {

    using Clock = std::chrono::steady_clock;

    // --- primary window --------------------------------------------------------------------

    std::unordered_map<WORKSPACEID, PHLWINDOWREF> g_anchors;

    struct {
        CHyprSignalListener open;
        CHyprSignalListener move;
    } g_listeners;

    bool hasOtherWindow(const PHLWINDOW& window, const PHLWORKSPACE& workspace) {
        for (const auto& other : Desktop::windowState()->windows()) {
            if (other && other != window && other->m_isMapped && other->m_workspace == workspace)
                return true;
        }
        return false;
    }

    // A window that lands on a workspace holding no other window is that workspace's first app.
    void claimIfFirst(const PHLWINDOW& window, const PHLWORKSPACE& workspace) {
        if (!window || !workspace || window->m_pinned || hasOtherWindow(window, workspace))
            return;
        g_anchors[workspace->m_id] = window;
    }

    void seedAnchors() {
        g_anchors.clear();
        std::unordered_map<WORKSPACEID, PHLWINDOW> oldest;
        for (const auto& window : Desktop::windowState()->windows()) {
            if (!window || !window->m_isMapped || window->m_pinned || !window->m_workspace)
                continue;
            auto& slot = oldest[window->m_workspace->m_id];
            if (!slot || window->m_stableID < slot->m_stableID)
                slot = window;
        }
        for (const auto& [id, window] : oldest)
            g_anchors[id] = window;
    }

    // --- icon lookup -----------------------------------------------------------------------

    std::string lower(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return std::tolower(c); });
        return value;
    }

    std::string trim(const std::string& value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }

    std::string envOr(const char* name, const std::string& fallback) {
        const char* value = std::getenv(name);
        return value && *value ? std::string{value} : fallback;
    }

    std::string home() {
        return envOr("HOME", "/");
    }

    // $XDG_DATA_HOME first, so a user's entry or icon overrides the system's.
    std::vector<std::string> dataDirs() {
        std::vector<std::string> dirs{envOr("XDG_DATA_HOME", home() + "/.local/share")};
        const std::string        system = envOr("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
        size_t                   start  = 0;
        while (start <= system.size()) {
            const size_t end = std::min(system.find(':', start), system.size());
            if (end > start)
                dirs.push_back(system.substr(start, end - start));
            start = end + 1;
        }
        return dirs;
    }

    bool isFile(const std::string& path) {
        std::error_code ec;
        return fs::is_regular_file(path, ec);
    }

    // class (lowercase) -> Icon= of the desktop entry that claims it
    std::unordered_map<std::string, std::string> g_desktopIcons;
    std::optional<Clock::time_point>             g_desktopIndexBuilt;

    void buildDesktopIndex() {
        g_desktopIcons.clear();
        g_desktopIndexBuilt = Clock::now();

        struct SEntry {
            std::string id, icon, wmClass, exec;
        };
        std::vector<SEntry> entries;
        for (const auto& dir : dataDirs()) {
            std::error_code ec;
            for (const auto& file : fs::directory_iterator(dir + "/applications", ec)) {
                if (file.path().extension() != ".desktop")
                    continue;
                std::ifstream in(file.path());
                SEntry        entry{.id = file.path().stem().string()};
                bool          inMain = false;
                for (std::string line; std::getline(in, line);) {
                    if (!line.empty() && line[0] == '[') {
                        inMain = line.starts_with("[Desktop Entry]");
                        continue;
                    }
                    if (!inMain)
                        continue;
                    if (line.starts_with("Icon="))
                        entry.icon = trim(line.substr(5));
                    else if (line.starts_with("StartupWMClass="))
                        entry.wmClass = trim(line.substr(15));
                    else if (line.starts_with("Exec="))
                        entry.exec = line.substr(5);
                }
                if (!entry.icon.empty())
                    entries.push_back(std::move(entry));
            }
        }

        // Strongest claim first; emplace keeps the first (and the user's dir comes first).
        for (const auto& entry : entries)
            g_desktopIcons.emplace(lower(entry.id), entry.icon);
        for (const auto& entry : entries) {
            if (!entry.wmClass.empty())
                g_desktopIcons.emplace(lower(entry.wmClass), entry.icon);
        }
        for (const auto& entry : entries) {
            if (const auto WEBAPP = Hyprexpo::webAppClassFromUrl(entry.exec); !WEBAPP.empty())
                g_desktopIcons.emplace(lower(WEBAPP), entry.icon);
        }
        for (const auto& entry : entries) {
            if (const auto DOT = entry.id.rfind('.'); DOT != std::string::npos)
                g_desktopIcons.emplace(lower(entry.id.substr(DOT + 1)), entry.icon);
        }
    }

    struct SThemeDir {
        std::string path;
        int         score = 0;
    };

    struct STheme {
        std::vector<SThemeDir>   dirs; // best first
        std::vector<std::string> inherits;
    };

    std::unordered_map<std::string, STheme> g_themes;

    std::vector<std::string> iconBases() {
        std::vector<std::string> bases{home() + "/.icons"};
        for (const auto& dir : dataDirs())
            bases.push_back(dir + "/icons");
        return bases;
    }

    std::vector<std::string> splitList(const std::string& value) {
        std::vector<std::string> out;
        size_t                   start = 0;
        while (start <= value.size()) {
            const size_t end = std::min(value.find(',', start), value.size());
            if (const auto ITEM = trim(value.substr(start, end - start)); !ITEM.empty())
                out.push_back(ITEM);
            start = end + 1;
        }
        return out;
    }

    const STheme& loadTheme(const std::string& name) {
        if (const auto IT = g_themes.find(name); IT != g_themes.end())
            return IT->second;

        STheme& theme = g_themes[name];

        struct SDirInfo {
            int  size = 0, scale = 1;
            bool scalable = false, apps = false;
        };
        std::vector<std::string>                  order;
        std::unordered_map<std::string, SDirInfo> info;
        bool                                      parsed = false;

        for (const auto& base : iconBases()) {
            const std::string root = base + "/" + name;
            if (parsed || !isFile(root + "/index.theme"))
                continue;
            parsed = true;

            std::ifstream in(root + "/index.theme");
            std::string   section;
            for (std::string line; std::getline(in, line);) {
                line = trim(line);
                if (line.starts_with("[")) {
                    section = line.substr(1, line.find(']') - 1);
                    continue;
                }
                const auto EQ = line.find('=');
                if (EQ == std::string::npos)
                    continue;
                const std::string key = trim(line.substr(0, EQ)), value = trim(line.substr(EQ + 1));
                if (section == "Icon Theme") {
                    if (key == "Directories" || key == "ScaledDirectories") {
                        for (const auto& dir : splitList(value)) {
                            if (!info.contains(dir))
                                order.push_back(dir);
                            info[dir];
                        }
                    } else if (key == "Inherits")
                        theme.inherits = splitList(value);
                    continue;
                }
                auto& dir = info[section];
                if (key == "Size")
                    dir.size = std::atoi(value.c_str());
                else if (key == "Scale")
                    dir.scale = std::max(1, std::atoi(value.c_str()));
                else if (key == "Type")
                    dir.scalable = value == "Scalable";
                else if (key == "Context")
                    dir.apps = value == "Applications" || value == "Apps";
            }
        }

        // Application icons first, then the sharpest: a scalable one, else the most pixels.
        for (const auto& dir : order) {
            const auto& d     = info[dir];
            const int   score = (d.apps ? 1'000'000 : 0) + (d.scalable ? 100'000 : d.size * d.scale);
            for (const auto& base : iconBases()) {
                std::error_code ec;
                if (fs::is_directory(base + "/" + name + "/" + dir, ec))
                    theme.dirs.push_back({base + "/" + name + "/" + dir, score});
            }
        }
        std::ranges::stable_sort(theme.dirs, [](const auto& a, const auto& b) { return a.score > b.score; });
        return theme;
    }

    std::string g_themeName;
    std::string g_themeChecked; // the value the caches below were built for
    std::optional<Clock::time_point> g_themeCheckedAt;

    std::string currentThemeName() {
        // Through the compat shim like every string key: under a Lua config the raw API's data
        // pointer for a string is not a `const char*` (it crashed the live session, 2026-09-25).
        if (const std::string configured = trim(std::string{CompatHyprlandAPI::stringValue("plugin:hyprexpo:label_icon_theme")}); !configured.empty())
            return configured;

        // Omarchy's theme switcher writes the icon theme it sets through gsettings here.
        std::ifstream in(envOr("XDG_STATE_HOME", home() + "/.local/state") + "/omarchy/current/theme/icons.theme");
        std::string   name;
        std::getline(in, name);
        return trim(name);
    }

    std::unordered_map<std::string, std::string> g_iconPaths; // icon name -> file ("" = none)

    std::string findIconFile(const std::string& icon) {
        if (icon.empty())
            return {};
        if (icon.front() == '/')
            return isFile(icon) ? icon : std::string{};
        if (const auto IT = g_iconPaths.find(icon); IT != g_iconPaths.end())
            return IT->second;

        std::vector<std::string> chain;
        auto                     visit = [&](auto&& self, const std::string& name, int depth) -> void {
            if (name.empty() || depth > 8 || std::ranges::find(chain, name) != chain.end())
                return;
            chain.push_back(name);
            for (const auto& parent : loadTheme(name).inherits)
                self(self, parent, depth + 1);
        };
        visit(visit, g_themeName, 0);
        visit(visit, "hicolor", 0);

        std::string found;
        for (const auto& name : chain) {
            for (const auto& dir : loadTheme(name).dirs) {
                for (const char* ext : {".svg", ".png"}) {
                    if (isFile(dir.path + "/" + icon + ext)) {
                        found = dir.path + "/" + icon + ext;
                        break;
                    }
                }
                if (!found.empty())
                    break;
            }
            if (!found.empty())
                break;
        }
        for (const char* ext : {".svg", ".png"}) {
            if (found.empty() && isFile("/usr/share/pixmaps/" + icon + ext))
                found = "/usr/share/pixmaps/" + icon + ext;
        }

        g_iconPaths[icon] = found;
        return found;
    }

    struct SClassIcon {
        std::string       path;
        Clock::time_point resolvedAt;
    };
    std::unordered_map<std::string, SClassIcon>           g_classIcons;
    std::unordered_map<std::string, SP<Render::ITexture>> g_textures; // "<px>:<path>"

    void dropCaches() {
        g_desktopIcons.clear();
        g_desktopIndexBuilt.reset();
        g_themes.clear();
        g_iconPaths.clear();
        g_classIcons.clear();
        g_textures.clear();
    }

    // A theme switch changes every icon, so the caches are rebuilt when it happens; the file
    // behind it is read at most every two seconds, and only while cards are being drawn.
    void refreshTheme() {
        const auto NOW = Clock::now();
        if (g_themeCheckedAt && NOW - *g_themeCheckedAt < std::chrono::seconds(2))
            return;
        g_themeCheckedAt = NOW;

        const auto NAME = currentThemeName();
        if (NAME != g_themeChecked) {
            dropCaches();
            g_themeChecked = NAME;
        }
        g_themeName = NAME;
    }

    std::string iconPathForClass(const std::string& appClass) {
        const auto KEY = lower(appClass);
        const auto NOW = Clock::now();
        if (const auto IT = g_classIcons.find(KEY); IT != g_classIcons.end()) {
            // A miss is retried after a while: the app may have been installed since.
            if (!IT->second.path.empty() || NOW - IT->second.resolvedAt < std::chrono::seconds(30))
                return IT->second.path;
        }

        if (!g_desktopIndexBuilt || (!g_desktopIcons.contains(KEY) && NOW - *g_desktopIndexBuilt > std::chrono::seconds(30))) {
            buildDesktopIndex();
            g_iconPaths.clear();
        }

        std::string path;
        if (const auto IT = g_desktopIcons.find(KEY); IT != g_desktopIcons.end())
            path = findIconFile(IT->second);
        if (path.empty())
            path = findIconFile(appClass);
        if (path.empty() && KEY != appClass)
            path = findIconFile(KEY);

        g_classIcons[KEY] = {path, NOW};
        return path;
    }

    SP<Render::ITexture> loadTexture(const std::string& path, int px) {
        Hyprgraphics::CImage image(path, Vector2D{(double)px, (double)px});
        if (!image.success())
            return nullptr;

        const auto SOURCE = image.cairoSurface();
        if (!SOURCE || SOURCE->status() != CAIRO_STATUS_SUCCESS)
            return nullptr;
        const auto SIZE = SOURCE->size();
        if (SIZE.x <= 0 || SIZE.y <= 0)
            return nullptr;

        // Fit into a px square, centred, keeping the aspect: the texture is drawn 1:1 in pixels.
        const auto   SURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
        const auto   CAIRO   = cairo_create(SURFACE);
        const double SCALE   = std::min(px / SIZE.x, px / SIZE.y);
        cairo_translate(CAIRO, (px - SIZE.x * SCALE) / 2.0, (px - SIZE.y * SCALE) / 2.0);
        cairo_scale(CAIRO, SCALE, SCALE);
        cairo_set_source_surface(CAIRO, SOURCE->cairo(), 0, 0);
        cairo_pattern_set_filter(cairo_get_source(CAIRO), CAIRO_FILTER_BEST);
        cairo_paint(CAIRO);
        cairo_surface_flush(SURFACE);

        auto tex = g_pHyprRenderer->createTexture(SURFACE);
        if (tex) {
            tex->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            tex->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }

        cairo_destroy(CAIRO);
        cairo_surface_destroy(SURFACE);
        return tex;
    }

}

void init() {
    seedAnchors();
    g_listeners.open = Event::bus()->m_events.window.open.listen([](PHLWINDOW window) {
        if (window)
            claimIfFirst(window, window->m_workspace);
    });
    g_listeners.move = Event::bus()->m_events.window.moveToWorkspace.listen([](PHLWINDOW window, PHLWORKSPACE workspace) { claimIfFirst(window, workspace); });
}

void shutdown() {
    g_listeners.open.reset();
    g_listeners.move.reset();
    g_anchors.clear();
    dropCaches();
    g_themeChecked.clear();
    g_themeCheckedAt.reset();
}

void onConfigReload() {
    dropCaches();
    g_themeCheckedAt.reset();
    g_themeChecked.clear();
}

PHLWINDOW anchorWindow(WORKSPACEID workspace) {
    const auto IT = g_anchors.find(workspace);
    return IT == g_anchors.end() ? nullptr : IT->second.lock();
}

void setAnchorWindow(WORKSPACEID workspace, const PHLWINDOW& window) {
    if (window)
        g_anchors[workspace] = window;
    else
        g_anchors.erase(workspace);
}

PHLWINDOW primaryWindow(WORKSPACEID workspace) {
    if (workspace == WORKSPACE_INVALID)
        return nullptr;

    std::vector<Hyprexpo::SPrimaryWindowCandidate> candidates;
    std::vector<PHLWINDOW>                         windows;
    for (const auto& window : Desktop::windowState()->windows()) {
        if (!window || !window->m_isMapped || window->m_pinned || !window->m_workspace || window->m_workspace->m_id != workspace)
            continue;
        const auto SIZE = window->sizeAnimation()->goal();
        candidates.push_back({.id = window->m_stableID, .area = SIZE.x * SIZE.y, .visible = !window->isHidden()});
        windows.push_back(window);
    }

    std::optional<uint64_t> anchor;
    if (const auto IT = g_anchors.find(workspace); IT != g_anchors.end()) {
        if (const auto WINDOW = IT->second.lock())
            anchor = WINDOW->m_stableID;
    }

    const auto ID = Hyprexpo::choosePrimaryWindow(anchor, candidates);
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].id == ID)
            return windows[i];
    }
    return nullptr;
}

SP<Render::ITexture> iconTexture(const std::string& appClass, int px) {
    if (appClass.empty() || px <= 0)
        return nullptr;

    refreshTheme();
    const auto PATH = iconPathForClass(appClass);
    if (PATH.empty())
        return nullptr;

    const auto KEY = std::to_string(px) + ":" + PATH;
    if (const auto IT = g_textures.find(KEY); IT != g_textures.end())
        return IT->second;

    auto tex        = loadTexture(PATH, px);
    g_textures[KEY] = tex; // a file that fails to load is not retried every frame
    return tex;
}

}
