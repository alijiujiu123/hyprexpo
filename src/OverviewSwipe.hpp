#pragma once

#include <cmath>

namespace Hyprexpo {

// A connected output can still have no usable mode after resume. Do not build
// previews or an overview camera until both coordinate spaces have a size.
template <typename Vector>
bool validOverviewSize(const Vector& logical, const Vector& pixels) {
    return std::isfinite(logical.x) && logical.x > 0 && std::isfinite(logical.y) && logical.y > 0 &&
        std::isfinite(pixels.x) && pixels.x > 0 && std::isfinite(pixels.y) && pixels.y > 0;
}

// resolve must identify one session generation, not just the monitor: release
// can synchronously destroy the session, or replace it through a callback.
template <typename Resolve>
void finishOverviewSwipe(Resolve&& resolve, bool selects, double projected) {
    auto* const session = resolve();
    if (!session || session->closeCommitted())
        return;

    session->setClosing(false);
    session->onSwipeEnd(selects, projected);
    if (auto* const survivor = resolve())
        survivor->resetSwipe();
}

} // namespace Hyprexpo
