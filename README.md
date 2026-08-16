# contrek-python

Python bindings for [Contrek](https://github.com/runout77/contrek), a
topology-preserving streaming polygonization engine for raster-to-vector
conversion.

This wrapper (MIT) is a thin pybind11 layer around Contrek's C++17 core
(AGPLv3). The upstream repo is vendored as a git submodule at
`vendor/contrek` (a submodule always points at a repo root, so we can't
mount just its `ext/cpp_polygon_finder/PolygonFinder` subfolder directly);
CMake then builds only that subfolder from within it, so the engine stays
canonical in a single upstream repo.

## Getting the code

```bash
git clone --recurse-submodules https://github.com/<your-user>/contrek-python.git
cd contrek-python
# or, if you already cloned without --recurse-submodules:
git submodule update --init --recursive
```

## Development install

```bash
python -m venv .venv && source .venv/bin/activate
pip install -e ".[test]"
pytest
```

Building compiles `ext/cpp_polygon_finder/PolygonFinder` (the submodule)
together with `src/bindings.cpp` via CMake/scikit-build-core — no manual
CMake invocation needed for a normal `pip install`.

## Usage

```python
import contrek

result = contrek.contour(
    "image.png",
    threads=4,
    tiles=4,
    versus=contrek.Versus.ANTICLOCKWISE,
    treemap=True,
)

print(result.groups, result.width, result.height)
print(result.benchmarks)

for poly in result.polygons:
    print(poly.outer)      # numpy.ndarray, shape (N, 2), dtype int32
    print(poly.inner)      # list[numpy.ndarray]
    print(poly.bounds)     # {"min_x": ..., "min_y": ..., "max_x": ..., "max_y": ...}
```

All point/coordinate data (`outer`, `inner`, `treemap`) comes back as
NumPy `int32` arrays of shape `(N, 2)`, ready for `shapely`, `matplotlib`,
or further NumPy processing -- not Python lists of objects.

## Platform support

Linux and macOS only, same constraint as the upstream engine (POSIX
threading primitives). Windows users: use WSL2.

## License

- Python wrapper code: MIT, see `LICENSE`.
- C++ core engine (submodule): AGPLv3. If you use it in a closed-source
  product you must either open your source or obtain a commercial license
  from the upstream author — see
  [runout77/contrek's license notes](https://github.com/runout77/contrek#license).

## Status

`⚠️ RectBounds field names (min_x, min_y, max_x, max_y) used in
bindings.cpp are ASSUMED from usage in ProcessResult::translate() --
verify against the real RectBounds struct definition in the upstream
repo (likely in Finder.h or a Geometry-ish header) and adjust
polygon_to_pydict() in src/bindings.cpp if they differ.`

Otherwise the binding surface is complete for v0.1: `Config` (all
fields + enums), `contour()` / `trace()`, and the full `ProcessResult`
→ `groups`, `width`, `height`, `versus`, `has_bounds`, `named_sequence`,
`benchmarks`, `treemap`, `polygons` (each with `outer`/`inner`/`bounds`).

Not yet bound: `ProcessResult::save_svg()` / `to_svg_stream()` /
`draw_on_bitmap()` -- straightforward to add as methods once you decide
whether Python should call back into the C++ SVG writer directly, or
just build SVGs from the NumPy arrays on the Python side (e.g. via
`svgwrite` or plain string formatting), which is probably more
idiomatic for a Python-first workflow.
