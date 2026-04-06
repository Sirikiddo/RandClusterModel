#pragma once

#include "LayeredState.h"

namespace proc {

// Port roles are fixed by protocol:
// - St_I and St_O always need full G/V/D layering.
// - St_S is lvl2 for local rollback (V+D) or lvl3 for global rollback (G/V/D).
template <class Policy = DefaultMemoryPolicy, template <class> class BaseStoreT = BaseStore, template <class> class OverlayStoreT = OverlayStore>
using InputPortState = LayeredState<3, Policy, BaseStoreT, OverlayStoreT>;

template <class Policy = DefaultMemoryPolicy, template <class> class BaseStoreT = BaseStore, template <class> class OverlayStoreT = OverlayStore>
using OutputPortState = LayeredState<3, Policy, BaseStoreT, OverlayStoreT>;

template <int LvlS, class Policy = DefaultMemoryPolicy, template <class> class BaseStoreT = BaseStore, template <class> class OverlayStoreT = OverlayStore>
using WorkPortState = LayeredState<LvlS, Policy, BaseStoreT, OverlayStoreT>;

} // namespace proc
