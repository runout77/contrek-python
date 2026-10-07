import hashlib
import os
import pathlib
import tempfile
from collections import deque
from concurrent.futures import ThreadPoolExecutor

import pytest

import contrek


def memory_usage():
    current_rss = None
    peak_rss = None

    with open("/proc/self/status") as status:
        for line in status:
            if line.startswith("VmRSS:"):
                current_rss = int(line.split()[1]) / 1024
            elif line.startswith("VmHWM:"):
                peak_rss = int(line.split()[1]) / 1024

    return current_rss, peak_rss


@pytest.mark.large
def test_streaming_concurrent():
    file_path = os.environ["FILE_PATH"]
    stripe_height = int(os.environ["STRIPE_HEIGHT"])
    threads = int(os.environ.get("THREADS", "1"))
    compare_hash = os.environ.get("COMPARE_HASH")

    source = contrek.PngSource(file_path)
    streamer = contrek.RasterStreamer(source, stripe_height)

    white = contrek.rgb_to_target_color(255, 255, 255, 255)

    with tempfile.NamedTemporaryFile(suffix=".svg", delete=False) as shared_stream:
        temp_path = shared_stream.name

    try:
        merger = contrek.SvgStreamingMerger(
            options={"bounds": True},
            output_path=temp_path,
            width=source.width,
            height=source.height,
        )

        buffer_bitmap = contrek.RawBitmap(source.width, streamer.stripe_height)
        jobs = deque()

        stripe_count = 0
        processed_rows = 0

        print()
        print(f"IMAGE:         {source.width}x{source.height}")
        print(f"STRIPE HEIGHT: {stripe_height}")
        print(f"THREADS:       {threads}")
        print()

        def find_stripe(stripe, bitmap, buffer_rows, rows_read):
            tile = contrek.find_polygons_raw(
                bitmap,
                options={
                    "processing_height": buffer_rows,
                    "versus": "o",
                    "bounds": True,
                },
                target_color=white,
                mode=contrek.MatchMode.NOT_COLOR,
                number_of_threads=0,
            )

            return stripe, rows_read, tile

        def merge_oldest():
            nonlocal processed_rows

            future = jobs.popleft()
            stripe, rows_read, tile = future.result()

            processed_rows += rows_read
            last = processed_rows == source.height

            merger.add_tile(tile, flush=last)
            del tile

            rss, peak_rss = memory_usage()

            print(
                f"stripe={stripe:<3} "
                f"processed={processed_rows}/{source.height} "
                f"rss={rss:.1f} MiB "
                f"peak={peak_rss:.1f} MiB"
            )

        with ThreadPoolExecutor(max_workers=threads) as executor:

            def consume(bitmap, buffer_rows, buffer_size, rows_read):
                nonlocal stripe_count
                if len(jobs) >= threads:
                    merge_oldest()
                stripe = stripe_count
                stripe_count += 1
                detached = bitmap.detach()
                jobs.append(
                    executor.submit(
                        find_stripe,
                        stripe,
                        detached,
                        buffer_rows,
                        rows_read,
                    )
                )

            streamer.each(buffer_bitmap, consume)
            while jobs:
                merge_oldest()

        result = merger.process_info()

        assert processed_rows == source.height
        assert result["width"] == source.width
        assert result["height"] == source.height

        with open(temp_path, "rb") as stream:
            stream_hash = hashlib.sha256(stream.read()).hexdigest()

        if compare_hash is not None:
            assert stream_hash == compare_hash

    finally:
        pathlib.Path(temp_path).unlink(missing_ok=True)
