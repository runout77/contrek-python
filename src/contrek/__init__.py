"""Python bindings for Contrek -- topology-preserving polygonization engine.

Thin, Pythonic layer on top of the compiled `_contrek` pybind11 extension.
The low-level `_contrek.trace()` call already returns NumPy arrays for
point data; this module just wraps the raw dict in small dataclasses so
IDEs get autocomplete and results are easy to inspect.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

import numpy as np
import enum

from . import _contrek
from ._version import __version__

# Re-export enums / Config so users don't need to import _contrek directly.
Versus = _contrek.Versus
MatchMode = _contrek.MatchMode
Connectivity = _contrek.Connectivity
Config = _contrek.Config

ResultVersus = enum.IntEnum(
    "ResultVersus",
    {"CLOCKWISE": _contrek.NODE_VERSUS_O, "ANTICLOCKWISE": _contrek.NODE_VERSUS_A},
)

# Low-level API: direct Bitmap / PolygonFinder access, bypassing the
# one-shot contour()/trace() convenience wrapper.
Identifier = _contrek.Identifier
Bitmap = _contrek.Bitmap
FastPngBitmap = _contrek.FastPngBitmap
find_polygons = _contrek.find_polygons

# Streaming API: progressive SVG/GeoJSON merge on disk, tile by tile.
find_polygons_raw = _contrek.find_polygons_raw
make_result_from_polygons = _contrek.make_result_from_polygons
RawProcessResult = _contrek.RawProcessResult
SvgStreamingMerger = _contrek.SvgStreamingMerger
GeoJsonStreamingMerger = _contrek.GeoJsonStreamingMerger
VerticalMerger = _contrek.VerticalMerger
HorizontalMerger = _contrek.HorizontalMerger

__all__ = [
    "contour",
    "ContourResult",
    "Polygon",
    "Config",
    "Versus",
    "MatchMode",
    "Connectivity",
    "rgb_to_target_color",
    "Identifier",
    "Bitmap",
    "FastPngBitmap",
    "find_polygons",
    "find_polygons_raw",
    "make_result_from_polygons",
    "RawProcessResult",
    "SvgStreamingMerger",
    "GeoJsonStreamingMerger",
    "VerticalMerger",
    "HorizontalMerger",
    "ResultVersus",
    "__version__",
]


def rgb_to_target_color(r: int, g: int, b: int, a: int = 255) -> int:
    """Pack an RGBA color into the int32 value Contrek expects for
    Config.target_color.

    Matches RawBitmap::rgb_value_at(), which reinterpret_casts the raw
    [R, G, B, A] bytes in memory as a single little-endian uint32_t:
    value = R | (G << 8) | (B << 16) | (A << 24).

    Values are wrapped into the signed int32 range since target_color
    is a signed field in Config (and -1 means "auto-detect from the
    pixel at (0, 0)").
    """
    unsigned = (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16) | ((a & 0xFF) << 24)
    # Config.target_color is int32_t; reinterpret the unsigned 32-bit
    # pattern as signed, same as the C++ side would see it.
    return unsigned - 0x1_0000_0000 if unsigned >= 0x8000_0000 else unsigned


@dataclass
class Polygon:
    """A single traced polygon.

    outer: (N, 2) int32 ndarray of the outer ring's [x, y] coordinates.
    inner: list of (M, 2) int32 ndarrays, one per hole.
    bounds: dict with min_x, min_y, max_x, max_y, is_empty.
    """

    outer: np.ndarray
    inner: list[np.ndarray]
    bounds: dict[str, int]

    @classmethod
    def _from_raw(cls, raw: dict[str, Any]) -> "Polygon":
        return cls(outer=raw["outer"], inner=raw["inner"], bounds=raw["bounds"])


@dataclass
class ContourResult:
    """Result of a `contour()` call, mirroring Contrek::ProcessResult."""

    groups: int
    width: int
    height: int
    versus: int
    has_bounds: bool
    named_sequence: str
    benchmarks: dict[str, float]
    treemap: np.ndarray
    polygons: list[Polygon] = field(default_factory=list)
    options: dict = field(default_factory=dict)

    @classmethod
    def _from_raw(cls, raw: dict[str, Any]) -> "ContourResult":
        return cls(
            groups=raw["groups"],
            width=raw["width"],
            height=raw["height"],
            versus=raw["versus"],
            has_bounds=raw["has_bounds"],
            named_sequence=raw["named_sequence"],
            benchmarks=raw["benchmarks"],
            treemap=raw["treemap"],
            polygons=[Polygon._from_raw(p) for p in raw["polygons"]],
            options=raw["options"],
        )


def contour(
    image_path: str,
    *,
    threads: int | None = None,
    tiles: int | None = None,
    versus: "Versus | None" = None,
    connectivity: "Connectivity | None" = None,
    mode: "MatchMode | None" = None,
    target_color: int | None = None,
    treemap: bool | None = None,
    bounds: bool | None = None,
    named_sequences: bool | None = None,
    unsafe_mode: bool | None = None,
    deterministic: bool | None = None,
    compress_unique: bool | None = None,
    compress_linear: bool | None = None,
    compress_raster: bool | None = None,
    compress_douglas_peucker: bool | None = None,
    compress_visvalingam: bool | None = None,
    compress_visvalingam_tolerance: float | None = None,
    config: "Config | None" = None,
) -> ContourResult:
    """Extract polygon contours from a raster image (e.g. PNG).

    Pass an existing `contrek.Config` via `config=`, or set individual
    options as keyword arguments (they're applied on top of `config`,
    or on top of a fresh `Config()` if `config` is omitted).
    """
    cfg = config if config is not None else Config()

    overrides = {
        "threads": threads,
        "tiles": tiles,
        "versus": versus,
        "connectivity_mode": connectivity,
        "mode": mode,
        "target_color": target_color,
        "treemap": treemap,
        "bounds": bounds,
        "named_sequences": named_sequences,
        "unsafe_mode": unsafe_mode,
        "deterministic": deterministic,
        "compress_unique": compress_unique,
        "compress_linear": compress_linear,
        "compress_raster": compress_raster,
        "compress_douglas_peucker": compress_douglas_peucker,
        "compress_visvalingam": compress_visvalingam,
        "compress_visvalingam_tolerance": compress_visvalingam_tolerance,
    }
    for attr, value in overrides.items():
        if value is not None:
            setattr(cfg, attr, value)

    raw = _contrek.trace(image_path, cfg)
    return ContourResult._from_raw(raw)