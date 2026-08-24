# contrek-python

Python bindings for [Contrek](https://github.com/runout77/contrek), a fast raster-to-vector polygon tracing engine written in C++. This wraps the C++ core with pybind11 so you can call it from Python without losing the performance. Coordinates come back as NumPy arrays and the underlying engine is multi-threaded. You can find more info in the main repo.

Wrapper is MIT. Core (vendored as a git submodule at `vendor/contrek`) is AGPLv3.

## Install

Source only, no prebuilt wheels — `pip install` compiles the core locally and tunes it for your CPU (`-march=native`, on by default in the core's own CMakeLists). Needs a C++17 compiler and CMake.

```bash
pip install contrek
```

For development (editable install, running tests):

```bash
git clone --recurse-submodules https://github.com/<your-user>/contrek-python.git
cd contrek-python
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[test]"
pytest
```

Linux/macOS only (POSIX threads).

## High-level API

Trace polygons from an image file in one call.

```python
import contrek

result = contrek.contour("image.png", number_ot_threads=4, number_ot_tiles=4, treemap=True)
print(result.groups, result.width, result.height)

for poly in result.polygons:
    print(poly.outer)   # numpy int32 (N, 2)
    print(poly.inner)   # list[numpy int32 (N, 2)]
    print(poly.bounds)  # {min_x, min_y, max_x, max_y, is_empty}
```

## Low-level API

Trace polygons from a bitmap you build yourself — either a PNG file (`FastPngBitmap`) or an in-memory pattern string (`Bitmap`, useful for synthetic tiles or tests).

```python
bitmap = contrek.Bitmap(pattern_string, width)  # or contrek.FastPngBitmap(path)

result = contrek.find_polygons(
    bitmap,
    options={"versus": "clockwise", "bounds": True, "compress": {"linear": True}},
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR,
)
```

`versus` accepts `"a"/"o"` for "anticlockwise"/"clockwise".

## Merging tiles in memory

`find_polygons_raw()` is the same as `find_polygons()` but returns a `RawProcessResult` handle instead of a dict, so it can be fed straight into a merger without converting to Python types first. `VerticalMerger`/`HorizontalMerger` combine multiple tiles into one result, in memory.

```python
tile = contrek.find_polygons_raw(bitmap, options={...}, target_color=..., mode=...)
tile2 = contrek.find_polygons_raw(bitmap, options={...}, target_color=..., mode=...)

merger = contrek.VerticalMerger(options={"bounds": True})   # or HorizontalMerger
merger.add_tile(tile)
merger.add_tile(tile2)
result = merger.process_info()
```

## Streaming to disk

`SvgStreamingMerger`/`GeoJsonStreamingMerger` write tiles to a file incrementally as they arrive, instead of holding every polygon in memory — useful for very large or many-tile jobs.

```python
merger = contrek.SvgStreamingMerger(options={"bounds": True}, output_path="out.svg", width=18, height=11)
for i, tile in enumerate(tiles):
    merger.add_tile(tile, flush=(i == len(tiles) - 1))
result = merger.process_info()
```

## Building results from raw polygon data

`make_result_from_polygons(polygons, width, height)` builds a `RawProcessResult` straight from polygon coordinates you already have — no bitmap or tracing involved. Useful for testing mergers, or feeding in geometry computed elsewhere.

## Tests

`tests/` also doubles as usage examples — see the various `test_*.py` files for more ways to call the API.

## License

- Wrapper: MIT (`LICENSE`)
- Core (submodule): AGPLv3 — [details](https://github.com/runout77/contrek#license)