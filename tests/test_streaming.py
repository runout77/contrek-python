import pathlib

import numpy as np
import pytest
import contrek
from fixture_helpers import assert_geojson_stream_matches, image_path, stream_fixture_path, fixture_path, load_expected_polygons, assert_polygons_match

def test_streaming_png():
  source = contrek.PngSource(str(image_path("labyrinth2.png")))
  streamer = contrek.RasterStreamer(source, stripe_height=20)
  buffer_bitmap = contrek.RawBitmap(source.width, streamer.stripe_height)
  finder = contrek.VerticalMerger(options={"compress": {"uniq": True, "linear": True}})

  def process_stripe(bitmap, buffer_rows, buffer_size, rows_read):
    tile = contrek.find_polygons_raw(
      bitmap,
      options = {
        "processing_height": buffer_rows,
        "versus": "o",
        "bounds": True,
      },
      target_color=contrek.rgb_to_target_color(255, 255, 255, 255),
      mode=contrek.MatchMode.NOT_COLOR,
    )
    finder.add_tile(tile)

  streamer.each(buffer_bitmap, process_stripe)

  result = finder.process_info()
  assert result["width"] == 130
  assert result["height"] == 130
  expected = load_expected_polygons(fixture_path("streaming", "streams_by_multiple_parts_argb.json"))
  assert_polygons_match(result["polygons"], expected)

def test_streaming_geotiff(tmp_path):
  source = contrek.TiffSource(
    str(image_path("pania_della_croce_wgs84.tif")),
    suppress_warnings=True,
  )
  streamer = contrek.RasterStreamer(source, stripe_height=20)
  buffer_bitmap = contrek.RawBitmap(source.width, streamer.stripe_height)
  localization = source.geo_localization
  assert localization["crs"] == {"authority": "EPSG", "code": 4326}

  output_path = str(tmp_path / "output.geojson")
  geo_finder = contrek.GeoJsonStreamingMerger(
    options={
      "geo_localization": localization,
      "compress": {
        "uniq": True,
        "linear": True,
      },
    },
    output_path=output_path,
    pixel_value=11,
  )

  total_height = 0
  def process_stripe(bitmap, buffer_rows, buffer_size, rows_read):
    nonlocal total_height
    tile = contrek.find_polygons_raw(
      bitmap,
      options={
        "processing_height": buffer_rows,
        "versus": "o",
        "bounds": True,
        "compress": {
          "uniq": True,
        },
      },
      target_color=contrek.rgb_to_target_color(255, 255, 255, 255),
      mode=contrek.MatchMode.NOT_COLOR,
    )
    total_height += rows_read
    geo_finder.add_tile(tile, total_height == source.height)


  streamer.each(buffer_bitmap, process_stripe)
  result = geo_finder.process_info()
  assert result["width"] == 64
  assert result["height"] == 64
  assert len(result["polygons"]) == 0

  with open(output_path, "r", encoding="utf-8") as f:
    geojson = f.read()
  assert_geojson_stream_matches(
    geojson,
    "test_64x64_contrek_cpp_cppgeojsonconcurrentstreamingmerger_w4.geojson",
  )