# System-exclusive three-finger gestures (planned)

Status: **not implemented** - tracked as an issue. This records what the goal is, what
Hyprland already gives us, and what a compositor-side patch would have to do.

## Goal

Three-finger swipes should behave like macOS: the compositor claims them, the action runs
with top priority, and the events are **not** delivered to the focused client, so no
application can swallow them. Today a client that implements `wp_pointer_gestures` swipes
(Chromium and everything Electron, for example) can take the gesture before the registered
trackpad gesture sees it - observed with Spotify fullscreen, where the three-finger up swipe
never reaches hyprexpo while the same gesture works over other Chromium windows.

## What exists

- `CTrackpadGestures::addGesture(gesture, fingers, direction, mods, deltaScale, disableInhibit)`
  and the matching `removeGesture`; the registration tuple is
  `(fingers, direction, mods, deltaScale, disableInhibit)`.
- `hl.gesture{...}` (core Lua API) and `hl.plugin.hyprexpo.gesture{...}` both expose
  `disable_inhibit`.
- **`disable_inhibit` is not the switch for this.** It only makes a gesture ignore
  `wp_keyboard_shortcuts_inhibit` (`managers/input/trackpad/TrackpadGestures.cpp:157,225`),
  which is what games, streaming and remote-desktop clients use. It has no effect on whether
  a client receives swipe events.
- Nothing in the plugin API lets a plugin declare "this gesture is exclusive", and the swipe
  forwarding to clients happens in the compositor (`CInputManager::onSwipeBegin/Update/End`
  -> `PROTO::pointerGestures->sendSwipe*`), not in a plugin-reachable path.

## What a patch would do

1. In `CInputManager::onSwipe{Begin,Update,End}`, look up
   `g_pTrackpadGestures->hasGesture(fingers, direction)` for the in-flight swipe and skip the
   `pointerGestures` send when one exists. That makes any registered trackpad gesture
   exclusive, for plugins and core gestures alike.
2. Cover every gesture the setup uses, otherwise a client can still take the uncovered
   direction: three fingers up/down (hyprexpo), three fingers horizontal (core workspace
   swipe), four fingers down (App Expose), three-finger pinch (application menu).
3. Keep registrations in sync: `edgebounce` replaces the core horizontal gesture by removing
   the tuple `(3, horizontal, 0, 1.0, false)`. Any change to a tuple (fingers/direction/mods/
   scale/disable_inhibit) silently stops matching, so the plugin and the patch have to land
   together.

## Cost

This is a Hyprland patch, not a plugin change: it means building and pinning our own
compositor and re-chasing it on every Hyprland release. Decision so far: implement later,
worth it only together with the rest of the "macOS-like" work.

## Workaround in the meantime

Keep a keyboard path for the affected action (`CTRL+UP` opens the overview today) and, when a
specific application is known to steal a direction, bind that action to a finger count the
application does not use.
