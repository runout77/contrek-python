# contrek-python

Python bindings for [Contrek](https://github.com/runout77/contrek), a fast raster-to-vector polygon tracing engine written in C++. This wraps the C++ core with pybind11 so you can call it from Python without losing the performance.

Wrapper is MIT. Core (vendored as a git submodule at `vendor/contrek`) is AGPLv3. You can find more info in the main repo.

## Install

Source only, no prebuilt wheels. Needs a C++17 compiler and CMake — `pip install` compiles the core locally and tunes it for your CPU (`-march=native`, on by default in the core's own CMakeLists).

```bash
git clone --recurse-submodules https://github.com/<your-user>/contrek-python.git
cd contrek-python
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[test]"
pytest
```

Linux/macOS only (POSIX threads).

## High-level API

```python
import contrek

result = contrek.contour("image.png", threads=4, tiles=4, treemap=True)

print(result.groups, result.width, result.height)
for poly in result.polygons:
    print(poly.outer)   # numpy int32 (N, 2)
    print(poly.inner)   # list[numpy int32 (N, 2)]
    print(poly.bounds)  # {min_x, min_y, max_x, max_y, is_empty}
```

Coordinates always come back as NumPy arrays, not lists of objects.

## Low-level API

Direct bitmap + finder access, for tile-based / synthetic / streaming workflows.

```python
bitmap = contrek.Bitmap(pattern_string, width)  # or contrek.FastPngBitmap(path)

result = contrek.find_polygons(
    bitmap,
    options={"versus": "clockwise", "bounds": True, "compress": {"linear": True}},
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR,
)
```

`versus` accepts `"a"/"o"` for "clockwise" - "anticlockwise".

`find_polygons_raw()` returns a `RawProcessResult` handle instead of a dict, for feeding into a merger without an intermediate conversion:

```python
tile = contrek.find_polygons_raw(bitmap, options={...}, target_color=..., mode=...)
tile2 = contrek.find_polygons_raw(bitmap, options={...}, target_color=..., mode=...)

merger = contrek.VerticalMerger(options={"bounds": True})   # or HorizontalMerger
merger.add_tile(tile)
merger.add_tile(tile2)
result = merger.process_info()
```

`SvgStreamingMerger` / `GeoJsonStreamingMerger` write tiles to disk incrementally instead of holding everything in memory:

```python
merger = contrek.SvgStreamingMerger(options={"bounds": True}, output_path="out.svg", width=18, height=11)
for i, tile in enumerate(tiles):
    merger.add_tile(tile, flush=(i == len(tiles) - 1))
result = merger.process_info()
```

`make_result_from_polygons(polygons, width, height)` builds a `RawProcessResult` from ready-made polygon data (no bitmap involved) — for feeding merger tests or externally-computed geometry.

## Tests

tests/ also doubles as usage examples — see the various test_*.py files for more ways to call the API.

## License

- Wrapper: MIT (`LICENSE`)
- Core (submodule): AGPLv3 — [details](https://github.com/runout77/contrek#license)