import pathlib
import time
import numpy as np
import pytest
import filecmp
import contrek
import pathlib
import io
import tempfile
from fixture_helpers import stream_fixture_path, fixture_path, load_expected_polygons, assert_polygons_match

@pytest.fixture
def sample_stripes():
  stripe1 =("00000000        "
            "00000000        "
            "00    00        "
            "00000000  000000"
            "00000000  000000"
            "          00  00")
  stripe2 =("          00  00"
            "          00  00"
            "0000000   00  00"
            "0000000   000000"
            "00   00   000000"
            "00   00         ")
  stripe3 =("00   00         "
            "00   00  0000000"
            "00   00  0000000"
            "00   00  00   00"
            "00   00  00   00"
            "00   00  0000000"
            "00   00  0000000"
            "00   00         ")
  stripe4 =("00   00         "
            "00   00         "
            "00   00         "
            "00   00         "
            "0000000         "
            "0000000         ")
  return [stripe1, stripe2, stripe3, stripe4]

def test_svg_streaming_merger(sample_stripes):
  width = 16
  height = 23

  with tempfile.NamedTemporaryFile(suffix=".svg", delete=False) as shared_stream:
    temp_path = shared_stream.name

  try:
    merger = contrek.SvgStreamingMerger(
        options={"bounds": True},
        output_path=temp_path,
        width=width,
        height=height,
    )
    for i, stripe in enumerate(sample_stripes):
      bitmap = contrek.Bitmap(stripe, width)
      tile = contrek.find_polygons_raw(
        bitmap,
        options={
            "versus": "o",
            "bounds": True,
            "compress": {"uniq": True, "linear": True},
        },
        target_color=ord("0"),
        mode=contrek.MatchMode.EXACT_COLOR
      )
      is_last = (i == len(sample_stripes) - 1)
      merger.add_tile(tile, flush=is_last)
    result = merger.process_info()
    assert result["groups"] == 4
    assert result["width"] == width
    assert result["height"] == height
    assert len(result["polygons"]) == 0
    assert  filecmp.cmp(str(stream_fixture_path("test_16x23_w4.svg")), temp_path, shallow=False) == True

  finally:
    pathlib.Path(temp_path).unlink(missing_ok=True)

def test_geojson_streaming_merger(sample_stripes):
  width = 16
  height = 23

  with tempfile.NamedTemporaryFile(suffix=".geojson", delete=False) as shared_stream:
    temp_path = shared_stream.name

  try:
    merger = contrek.GeoJsonStreamingMerger(
        options={"bounds": True},
        output_path=temp_path,
        pixel_value=34
    )
    for i, stripe in enumerate(sample_stripes):
      bitmap = contrek.Bitmap(stripe, width)
      tile = contrek.find_polygons_raw(
        bitmap,
        options={
            "versus": contrek.Identifier("o"),
            "bounds": True,
            "compress": {"uniq": True, "linear": True},
        },
        target_color=ord("0"),
        mode=contrek.MatchMode.EXACT_COLOR
      )
      is_last = (i == len(sample_stripes) - 1)
      merger.add_tile(tile, flush=is_last)
    result = merger.process_info()
    assert result["groups"] == 4
    assert result["width"] == width
    assert result["height"] == height
    assert len(result["polygons"]) == 0
    assert  filecmp.cmp(str(stream_fixture_path("test_16x23_w4.geojson")), temp_path, shallow=False) == True

  finally:
    pathlib.Path(temp_path).unlink(missing_ok=True)


@pytest.fixture
def triangle_stripes():
  stripe1 =("     000       000"
            "    00 00     00 0"
            "   00   00   00  0"
            "    00 00   00   0"
            "     000   00    0"
            "          00     0")
  stripe2 =("          00     0"
            " 000     00      0"
            "00 00   00       0"
            " 000   00        0"
            "      00         0"
            "     0000000000000")
  return [stripe1, stripe2]

def test_geojson_streaming_merger_with_compression(triangle_stripes):
  width = 18
  height = 11

  with tempfile.NamedTemporaryFile(suffix=".geojson", delete=False) as shared_stream:
    temp_path = shared_stream.name

  try:
    merger = contrek.GeoJsonStreamingMerger(
        options={"bounds": True, "compress": {"uniq": True, "linear": True, "douglas_peucker": True}},
        output_path=temp_path,
        pixel_value=34
    )
    for i, stripe in enumerate(triangle_stripes):
      bitmap = contrek.Bitmap(stripe, width)
      tile = contrek.find_polygons_raw(
        bitmap,
        options={
          "versus": contrek.Identifier("o"),
          "bounds": True,
        },
        target_color=ord("0"),
        mode=contrek.MatchMode.EXACT_COLOR
      )
      is_last = (i == len(triangle_stripes) - 1)
      merger.add_tile(tile, flush=is_last)

    result = merger.process_info()
    assert result["groups"] == 3
    assert result["width"] == width
    assert result["height"] == height
    assert len(result["polygons"]) == 0
    assert result["versus"] == contrek.ResultVersus.ANTICLOCKWISE
    assert  filecmp.cmp(str(stream_fixture_path("test_18x11_w2.geojson")), temp_path, shallow=False) == True

  finally:
    pathlib.Path(temp_path).unlink(missing_ok=True)

def test_svg_streaming_merger_with_compression(triangle_stripes):
  width = 18
  height = 11

  with tempfile.NamedTemporaryFile(suffix=".svg", delete=False) as shared_stream:
    temp_path = shared_stream.name

  try:
    merger = contrek.SvgStreamingMerger(
        options={"bounds": True, "compress": {"uniq": True, "linear": True, "douglas_peucker": True}},
        output_path=temp_path,
        width=width,
        height=height
    )
    for i, stripe in enumerate(triangle_stripes):
      bitmap = contrek.Bitmap(stripe, width)
      tile = contrek.find_polygons_raw(
        bitmap,
        options={
          "versus": "a",
          "bounds": True,
        },
        target_color=ord("0"),
        mode=contrek.MatchMode.EXACT_COLOR
      )
      is_last = (i == len(triangle_stripes) - 1)
      merger.add_tile(tile, flush=is_last)

    result = merger.process_info()
    assert result["groups"] == 3
    assert result["width"] == width
    assert result["height"] == height
    assert len(result["polygons"]) == 0
    assert  filecmp.cmp(str(stream_fixture_path("test_18x11_w2.svg")), temp_path, shallow=False) == True

  finally:
    pathlib.Path(temp_path).unlink(missing_ok=True)

def test_vertical_merger():
  up = ("   00000                      "
        "    00000000000               "
        "  000        00               "
        "   0         00               "
        " 00          00               "
        " 00          00               ")
  down = (" 00          00               "
          " 00          00               "
          " 00          00               "
          " 00000000000000               "
          " 00000000000000               ")

  result_up = contrek.find_polygons_raw(
    contrek.Bitmap(up, 30),
    options={
      "versus": "a",
      "bounds": True,
    },
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR
  )
  result_down = contrek.find_polygons_raw(
    contrek.Bitmap(down, 30),
    options={
      "versus": "a",
      "bounds": True,
    },
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR
  )
  assert result_down.to_dict()["versus"] == int(contrek.Versus.CLOCKWISE)

  merger = contrek.VerticalMerger()
  merger.add_tile(result_up)
  merger.add_tile(result_down)

  result = merger.process_info()
  assert result["groups"] == 1
  assert result["width"] == 30
  assert result["height"] == 10
  assert len(result["polygons"]) == 1

  expected = load_expected_polygons(fixture_path("concurrent", "merging", "merge_mode_example_14_vertical_bug_fixing.json"))
  assert_polygons_match(result["polygons"], expected)

def test_horizontal_merger():
  left=("0000000000"
        "0000000000"
        "00        "
        "00        "
        "00        "
        "00        "
        "00        "
        "0000000000"
        "0000000000")
  right=("0000000000"
         "0000000000"
         "      0000"
         "      0000"
         "      0000"
         "      0000"
         "      0000"
         "0000000000"
         "0000000000")
  result_left = contrek.find_polygons_raw(
    contrek.Bitmap(left, 10),
    options={
      "versus": "a",
      "bounds": True,
      "compress": {"uniq": True, "linear": True}
    },
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR
  )
  result_right = contrek.find_polygons_raw(
    contrek.Bitmap(right, 10),
    options={
      "versus": "a",
      "bounds": True,
      "compress": {"uniq": True, "linear": True}
    },
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR
  )
  merger = contrek.HorizontalMerger()
  merger.add_tile(result_left)
  merger.add_tile(result_right)

  result = merger.process_info()
  assert result["groups"] == 1
  assert result["width"] == 19
  assert result["height"] == 9
  assert len(result["polygons"]) == 1

  expected = load_expected_polygons(fixture_path("concurrent", "merging", "merge_mode.json"))
  assert_polygons_match(result["polygons"], expected)

def test_exceptions():
  left =("0000000000"
         "0         "
         "0         "
         "0000000000")
  right=("0000000000"
         "         0"
         "         0"
         "0000000000")
  right_higher =("0000000000"
                 "         0"
                 "         0"
                 "         0"
                 "0000000000")

  opts = {
    "versus": "a",
    "bounds": True,
    "compress": {"uniq": True, "linear": True},
  }
  result_left = contrek.find_polygons_raw(
    contrek.Bitmap(left, 10),
    options=opts,
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR
  )
  assert result_left.to_dict()["options"] == opts
  assert result_left.to_dict()["versus"] == contrek.ResultVersus.ANTICLOCKWISE

  opts_o = {
    "versus": "o",
    "bounds": True,
    "compress": {"uniq": True, "linear": True},
  }
  result_right = contrek.find_polygons_raw(
    contrek.Bitmap(left, 10),
    options=opts_o,
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR
  )
  assert result_right.to_dict()["options"] == opts_o
  assert result_right.to_dict()["versus"] == contrek.ResultVersus.CLOCKWISE

  merger = contrek.HorizontalMerger()
  merger.add_tile(result_left)

  with pytest.raises(ValueError, match="All results must have the same versus option"):
    merger.add_tile(result_right)

  result_higher = contrek.find_polygons_raw(
    contrek.Bitmap(right_higher, 10),
    options=opts,
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR
  ) 
  assert result_higher.to_dict()["options"] == opts
  assert result_higher.to_dict()["height"] == 5
  with pytest.raises(ValueError, match="All results must have the same height"):
    merger.add_tile(result_higher)
  
  result_compressed = contrek.find_polygons_raw(
    contrek.Bitmap(right, 10),
    options={
      "versus": "a",
      "bounds": True,
      "compress": {"visvalingam": True, "visvalingam_tolerance": 1.5},
    },
    target_color=ord("0"),
    mode=contrek.MatchMode.EXACT_COLOR
  ) 
  with pytest.raises(ValueError, match="Result with not supported postprocessing compression mode"):
    merger.add_tile(result_compressed)


def test_merging_existings_coordinates():
  polygons_up = [{
    "outer": [[0,0],[0,5],[2,5],[2,2],[9,2],[9,5],[11,5],[11,0]],
    "inner": [],
    "bounds": {"min_x": 0, "max_x": 11, "min_y": 0, "max_y": 5},
  }]
  result_up = contrek.make_result_from_polygons(polygons_up, width=12, height=5)

  polygons_down = [{
      "outer": [[0,0],[0,4],[11,4],[11,0],[9,0],[9,2],[2,2],[2,0]],
      "inner": [],
      "bounds": {"min_x": 0, "max_x": 11, "min_y": 0, "max_y": 4},
  }]
  result_down = contrek.make_result_from_polygons(polygons_down, width=12, height=5)

  merger = contrek.VerticalMerger(options={"unsafe_mode": True})
  merger.add_tile(result_up)
  merger.add_tile(result_down)
  result = merger.process_info()

  assert result["width"] == 12
  assert result["height"] == 9
  expected = load_expected_polygons(fixture_path("concurrent", "merging", "merge_mode_from_existing_polygons.json"))
  assert_polygons_match(result["polygons"], expected)