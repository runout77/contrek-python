"""Helpers for comparing Contrek results against Ruby-generated JSON
fixtures (e.g. exported from the Ruby test suite's expected outputs).
"""
import json
import numpy as np
import pathlib

CONTREK_ROOT = pathlib.Path(__file__).parent.parent / "vendor" / "contrek"
FIXTURES_DIR = CONTREK_ROOT / "spec" / "files" / "fixtures"

def fixture_path(*parts):
    """Build a path under the vendored Contrek fixtures directory.

    Usage: fixture_path("concurrent", "merging", "merge_mode_from_existing_polygons.json")
    """
    return FIXTURES_DIR.joinpath(*parts)

STREAMS_DIR = CONTREK_ROOT / "spec" / "files" / "streams"


def stream_fixture_path(*parts):
    """Build a path under the vendored Contrek streaming-output fixtures
    directory (expected SVG/GeoJSON files for streaming merger tests).

    Usage: stream_fixture_path("test_18x11_w2.svg")
    """
    return STREAMS_DIR.joinpath(*parts)

def load_expected_polygons(json_path):
    """Load a fixture JSON (list of {"outer": [{"x":.., "y":..}, ...],
    "inner": [[{"x":..,"y":..}, ...], ...]}) into the same shape as
    Contrek's Python result["polygons"]: list of dicts with 'outer' and
    'inner' as (N, 2) int32 ndarrays.
    """
    with open(json_path) as f:
        raw = json.load(f)

    polygons = []
    for poly in raw:
        outer = np.array([[p["x"], p["y"]] for p in poly["outer"]], dtype=np.int32)
        inner = [
            np.array([[p["x"], p["y"]] for p in ring], dtype=np.int32)
            for ring in poly.get("inner", [])
        ]
        polygons.append({"outer": outer, "inner": inner})
    return polygons


def assert_polygons_match(actual_polygons, expected_polygons):
    """Compare actual result['polygons'] against fixture-loaded polygons."""
    assert len(actual_polygons) == len(expected_polygons), (
        f"Expected {len(expected_polygons)} polygons, got {len(actual_polygons)}"
    )
    for i, (actual, expected) in enumerate(zip(actual_polygons, expected_polygons)):
        assert actual["outer"].shape == expected["outer"].shape, (
            f"Polygon {i}: outer shape mismatch "
            f"({actual['outer'].shape} vs {expected['outer'].shape})"
        )
        assert (actual["outer"] == expected["outer"]).all(), (
            f"Polygon {i}: outer points differ"
        )
        assert len(actual["inner"]) == len(expected["inner"]), (
            f"Polygon {i}: inner ring count differs"
        )
        for j, (a_ring, e_ring) in enumerate(zip(actual["inner"], expected["inner"])):
            assert (a_ring == e_ring).all(), f"Polygon {i}, inner ring {j}: points differ"