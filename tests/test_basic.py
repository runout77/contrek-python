import pathlib

import numpy as np
import pytest

import contrek

SAMPLE_PNG = pathlib.Path(__file__).parent / "images" / "labyrinth3.png"


@pytest.mark.skipif(not SAMPLE_PNG.exists(), reason="add a sample PNG under tests/files/")
def test_contour_basic_shape():
  result = contrek.contour(str(SAMPLE_PNG), threads=2, tiles=2, bounds=True)

  assert result.groups == 1
  assert result.width == 260
  assert result.height == 260
  assert isinstance(result.benchmarks, dict)
  assert isinstance(result.treemap, np.ndarray)

  for poly in result.polygons:
    assert poly.outer.ndim == 2
    assert poly.outer.shape[1] == 2
    assert poly.outer.dtype == np.int32
    for ring in poly.inner:
      assert ring.shape[1] == 2
    assert set(poly.bounds) == {"min_x", "min_y", "max_x", "max_y", "is_empty"}
    if not poly.bounds["is_empty"]:
      assert poly.bounds["min_x"] == 6
      assert poly.bounds["max_x"] == 253
      assert poly.bounds["min_y"] == 6
      assert poly.bounds["max_y"] == 253

def test_multithread():
  image = pathlib.Path(__file__).parent / "images" / "sample_10240x10240.png"
  color = contrek.rgb_to_target_color(255, 255, 255) # white
  result = contrek.contour(str(image), threads=8, tiles=8, target_color=color, mode=contrek.MatchMode.NOT_COLOR)
  print(result.benchmarks)
  assert result.groups == 8730

def test_config_defaults():
  cfg = contrek.Config()
  assert cfg.threads == 4
  assert cfg.tiles == 2
  assert cfg.target_color == -1
  assert cfg.target_color == -1

