import pathlib

import numpy as np
import pytest
import contrek
from fixture_helpers import image_path

def test_contour_basic_shape():
  result = contrek.contour(str(image_path("labyrinth3.png")), number_of_threads=2, number_of_tiles=2, bounds=True)

  assert result.groups == 1
  assert result.width == 260
  assert result.height == 260
  assert result.number_of_threads == 2
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
  color = contrek.rgb_to_target_color(255, 255, 255) # white
  result = contrek.contour(str(image_path("sample_10240x10240.png")), number_of_threads=8, number_of_tiles=8, target_color=color, mode=contrek.MatchMode.NOT_COLOR)
  assert result.groups == 8730
  assert result.number_of_threads == 8
  assert result.benchmarks['outer'] != 0

def test_monothread_find_polygons():
  bitmap = contrek.FastPngBitmap(str(image_path("graphs_1024x1024.png")))
  result = contrek.find_polygons(
    bitmap,
    options={
      "versus": "a",
      "compress": {"uniq": True, "linear": True},
    },
    target_color=contrek.rgb_to_target_color(255, 255, 255, 255),
    mode=contrek.MatchMode.NOT_COLOR
  )
  assert result['groups'] == 258
  assert result['number_of_threads'] == 0

def test_multithread_find_polygons():
  bitmap = contrek.FastPngBitmap(str(image_path("graphs_1024x1024.png")))
  result = contrek.find_polygons(
    bitmap,
    number_of_threads=4,
    options={
      "number_of_tiles": 4,
      "versus": "o",
      "bounds": True,
      "compress": {"uniq": True, "linear": True},
    },
    target_color=contrek.rgb_to_target_color(255, 255, 255, 255),
    mode=contrek.MatchMode.NOT_COLOR
  )
  assert result['groups'] == 258
  assert result['benchmarks']['inner'] != 0
  assert result['benchmarks']['outer'] != 0
  assert result['number_of_threads'] == 4

def test_config_defaults():
  cfg = contrek.Config()
  assert cfg.number_of_threads == 0
  assert cfg.number_of_tiles == 1
  assert cfg.target_color == -1
  assert cfg.target_color == -1

def test_bitmap():
  print(contrek.Identifier)
  print(contrek.Bitmap)
  print(contrek.FastPngBitmap)
  print(contrek.find_polygons)

  pattern = (
    "0000000"
    "0111100"
    "0111100"
    "0000000"
  )
  bitmap = contrek.Bitmap(pattern, 7)
  assert bitmap.w() == 7
  assert bitmap.h() == 4

  result = contrek.find_polygons(
      bitmap,
      options={"versus": "a", "bounds": True,"compress": {"linear": True}},
      target_color=ord("0"),
      mode=contrek.MatchMode.NOT_COLOR,
  )
  assert result["groups"] == 1
  assert result["width"] == 7
  assert result["height"] == 4
  assert len(result["polygons"]) == 1
  poly = result["polygons"][0]
  expected = np.array([[1, 1], [1, 3], [5, 3], [5, 1]])
  assert (poly['outer'] == expected).all()
  assert result["versus"] == contrek.ResultVersus.ANTICLOCKWISE
  assert result["options"] == {'bounds': True, 'compress': {'linear': True}, 'versus': 'a'}
