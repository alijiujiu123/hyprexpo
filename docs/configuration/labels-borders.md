# Labels and Borders

HyprExpo can render workspace labels, separate selection labels, and distinct borders for current, focused, and hovered tiles.

## Tile Appearance

| key | type | description | default |
| --- | --- | --- | --- |
| `plugin:hyprexpo:tile_rounding` | int | corner radius in pixels | `0` |
| `plugin:hyprexpo:tile_rounding_power` | float | rounding curve exponent | `2.0` |
| `plugin:hyprexpo:tile_rounding_focus` | int | focused tile radius, `-1` inherits | `-1` |
| `plugin:hyprexpo:tile_rounding_current` | int | current tile radius, `-1` inherits | `-1` |
| `plugin:hyprexpo:tile_rounding_hover` | int | hovered tile radius, `-1` inherits | `-1` |
| `plugin:hyprexpo:border_width` | int | border thickness in pixels | `2` |
| `plugin:hyprexpo:border_color` | string | default border for non-highlighted tiles; solid or gradient | empty |
| `plugin:hyprexpo:border_color_current` | string | current tile border; solid or gradient | `rgb(66ccff)` |
| `plugin:hyprexpo:border_color_focus` | string | focused tile border; solid or gradient | `rgb(ffcc66)` |
| `plugin:hyprexpo:border_color_hover` | string | hovered tile border; solid or gradient | `rgb(aabbcc)` |

Deprecated fallback keys are still recognized for compatibility: `border_grad_current`, `border_grad_focus`, `border_grad_hover`, and `border_style`. New configs should use `border_color_*`.

## Drag-Drop Window Styling

Drag-drop window movement uses a translucent proxy under the pointer, a
source-workspace border, and a positional landing proxy inside the hovered
target tile while the move is active. By default those visuals keep the existing
proxy colors and inherit the focused tile border; set the `drag_drop_*` keys
when you want the window-moving feedback to match your theme.

The target-tile landing proxy is a visual drop-intent preview. It shows where
the grabbed window would land based on pointer position and grab offset, but the
current release behavior still uses Hyprland's safe workspace move path. Floating
positional release and tiled layout-aware insertion are deferred follow-up work.

| key | type | description | default |
| --- | --- | --- | --- |
| `plugin:hyprexpo:drag_drop_proxy_color` | color | proxy fill before the drag crosses the move threshold | `0x24EDB342` |
| `plugin:hyprexpo:drag_drop_proxy_active_color` | color | proxy fill while a drag/drop move is active | `0x3DEDB342` |
| `plugin:hyprexpo:drag_drop_proxy_border_color` | string | proxy border, solid or gradient; empty inherits focus border | empty |
| `plugin:hyprexpo:drag_drop_proxy_border_width` | int | proxy border width; `-1` inherits, `0` disables | `-1` |
| `plugin:hyprexpo:drag_drop_proxy_rounding` | int | proxy corner radius; `-1` inherits automatic focused rounding | `-1` |
| `plugin:hyprexpo:drag_drop_source_border_color` | string | source workspace border during active drag/drop movement | empty |
| `plugin:hyprexpo:drag_drop_source_border_width` | int | source workspace border width; `-1` inherits, `0` disables | `-1` |

Example:

```ini
plugin {
    hyprexpo {
        drag_drop_proxy_color = rgba(66ccff22)
        drag_drop_proxy_active_color = rgba(66ccff44)
        drag_drop_proxy_border_color = rgba(66ccffee) rgba(ffcc66ee) 45deg
        drag_drop_source_border_color = rgb(ffcc66)
        drag_drop_proxy_border_width = 3
        drag_drop_proxy_rounding = 10
    }
}
```

## Workspace Labels

| key | type | description | default |
| --- | --- | --- | --- |
| `plugin:hyprexpo:label_enable` | bool int | enable workspace labels | `1` |
| `plugin:hyprexpo:label_text_mode` | string | `token`, `index`, or `id` | `token` |
| `plugin:hyprexpo:label_token_map` | string | comma-separated tokens by visible tile order; empty entries skip | empty |
| `plugin:hyprexpo:label_position` | string | `top-left`, `top-right`, `bottom-left`, `bottom-right`, or `center` | `center` |
| `plugin:hyprexpo:label_show` | string | `always`, `hover`, `focus`, `hover+focus`, `current+focus`, or `never` | `always` |
| `plugin:hyprexpo:label_font_size` | int | base font size in pixels | `16` |
| `plugin:hyprexpo:label_font_family` | string | Pango font family | `sans` |
| `plugin:hyprexpo:label_bg_enable` | bool int | draw a background bubble behind labels | `1` |
| `plugin:hyprexpo:label_bg_color` | color | label background color | `rgba(00000088)` |
| `plugin:hyprexpo:label_bg_shape` | string | `circle`, `square`, or `rounded` | `circle` |
| `plugin:hyprexpo:label_app_icon` | bool int | a card whose workspace has windows shows its primary app's icon instead of the text label (the first-opened window while it is open, else the largest one); empty cards keep the text | `0` |
| `plugin:hyprexpo:label_icon_size` | int | app icon edge in logical px (`0` = `label_font_size`) | `0` |
| `plugin:hyprexpo:label_icon_theme` | string | icon theme searched before hicolor (empty = Omarchy's current theme, `~/.local/state/omarchy/current/theme/icons.theme`) | empty |

### App icons

With `label_app_icon = 1` the badge becomes the icon of the workspace's *primary app*: the window
that was opened first on that workspace, for as long as it stays there; once it is closed or moved
away, the window with the largest on-screen area. The icon is found the way a launcher finds it:
the desktop entry whose id, `StartupWMClass` or web-app URL (Chromium `--app` windows, e.g.
Omarchy's web apps) matches the window class, then its `Icon=` through the icon theme, hicolor
and `/usr/share/pixmaps`. The icon is drawn without the background bubble, at `label_position`
and `label_offset_x/y`. A card with no windows, or whose app has no icon, keeps its text label.

### Reordering cards

With `drag_drop_enable = 1` on the dynamic grid (and `mru_sort = 0`), pressing a card's badge (the
app icon, or the text label of an empty card) and dragging picks up the **whole card**; pressing
anywhere else on a card still drags a single window. Dropping the card on another slot reorders:
the dragged card lands in that slot and the cards in between shift by one. A plain click on the
badge still selects the card.

A slot is a workspace, and workspace ids cannot change, so the reorder moves **windows**: the
dragged workspace's windows move into the target slot's workspace and every workspace in between
passes its windows one slot along. Anything that orders workspaces by id (keybinds, bars, this grid)
therefore agrees with the new order without any stored state. Fullscreen state and window groups
move with their windows, and the app-icon badge keeps each workspace's first-opened window. Your
current workspace keeps its id: if its slot's contents change, that is what you see after closing.
Reordering never crosses monitors.

## Selection Labels

Selection labels are optional overlays used by `hyprexpo:kb_select`. They let normal workspace labels stay stable while selection tokens use a separate map.

| key | type | description | default |
| --- | --- | --- | --- |
| `plugin:hyprexpo:selection_label_enable` | bool int | enable the separate selection-token overlay | `0` |
| `plugin:hyprexpo:selection_label_token_map` | string | comma-separated tokens by visible tile order; empty entries skip | `a,s,d,f,g,q,w,e,r,t,z,x,c,v,b` |
| `plugin:hyprexpo:selection_label_position` | string | label anchor | `top-right` |
| `plugin:hyprexpo:selection_label_color` | color | selection-token text color | `rgb(ffcc66)` |
