# Virtual ScrollRect / retained-card recycling (v0.18)

`AsmUiVirtualCarousel` separates the number of logical records from the number
of retained UI nodes. A song database may contain 100,000 entries while the UI
keeps only seven card roots alive.

For each frame the virtualizer chooses a contiguous window around the current
snap-scroll position. `asmui_virtual_carousel_slot()` maps physical slot `s` to
a logical index and returns the same position/scale/alpha visual used by the
normal carousel. Near the head and tail the window is clamped, so slots stay
full without reading outside the catalog.

The live Song Select has a retained `ASMUI_NODE_CONTAINER` at `(90,-318)` with
`ASMUI_CLIP` and size `1080x104`. The seven physical cards are children of this
container. This gives a RectMask/ScrollRect-style scissor before any fragment
shader work, and prevents off-viewport cards from leaking into the rest of the
page.

Pointer hit-testing is also virtualized: it loops over retained slots only,
converts the viewport-local card geometry back to canvas coordinates, and
returns a logical index. Dragging and wheel input continue to update the shared
snap-scroll state.

Regression target:

```text
logical songs   100,000
retained slots        7
retained nodes       68
visible draws        66
worst batches        41
```

`tests/virtual_list_profile.c` repeatedly jumps across the 100k-entry catalog
and requires the retained workload to stay bounded. The default p95 guardrail
is intentionally loose (`1000 us`) for portability and can be tightened with:

```bash
ASMUI_VIRTUAL_CPU_P95_US=100 make perf-virtual
```

The demo repeats seven metadata records for convenience. Real applications are
expected to supply their own catalog/model and rebind a physical slot whenever
its logical index changes.
