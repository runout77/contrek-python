// Python bindings for Contrek's C++17 core (ContrekApi.h / Finder.h).
//
// Design notes:
//  - Contrek::trace() returns a move-only TraceContext wrapping
//    ProcessResult (via unique_ptr). We don't expose TraceContext to
//    Python directly; instead we eagerly convert everything useful into
//    plain Python objects / NumPy arrays inside the trace() lambda
//    below, so the returned object has clean, GC-friendly ownership.
//  - Point is two ints -> outer/inner rings become numpy.ndarray(int32)
//    of shape (N, 2), which is both fast and what geometry/plotting
//    libraries (shapely, matplotlib, numpy) expect.
//  - RectBounds fields: {min_x, max_x, min_y, max_y}, plus is_empty()
//    (an "empty" bounds means min_x is still std::numeric_limits<int>::max(),
//    i.e. .expand() was never called -- we surface is_empty explicitly
//    so callers don't need to know that INT_MAX convention).

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>

#include "ContrekApi.h"
// Low-level API headers (for direct PolygonFinder / Bitmap access,
// bypassing the one-shot Contrek::trace() convenience wrapper).
#include "PolygonFinder.h"
#include "concurrent/Finder.h"
#include "Bitmap.h"
#include "FastPngBitmap.h"
#include "Options.h"
#include "OptionValue.h"
#include "RGBMatcher.h"
#include "RGBNotMatcher.h"
#include "SvgStreamingMerger.h"
#include "GeoJsonStreamingMerger.h"
#include "VerticalMerger.h"
#include "HorizontalMerger.h"

namespace py = pybind11;

namespace {

// --- Python -> C++ Options conversion --------------------------------
//
// Options is a dynamic string-keyed map of variant values (bool, int,
// double, string, Identifier, or nested Options) -- the C++ analogue of
// a Ruby Hash with symbol values like {versus: :a, bounds: true}.
// We mirror that from a plain Python dict:
//   - bool / int / float / str map directly
//   - contrek.Identifier("a") maps to an Identifier (Ruby's :a symbol)
//   - a nested dict maps to a nested Options
Options pyobj_to_options(const py::dict& d);

OptionValue pyobj_to_optionvalue(const py::handle& obj) {
    if (py::isinstance<py::bool_>(obj)) {
        return OptionValue(obj.cast<bool>());
    }
    if (py::isinstance<Identifier>(obj)) {
        return OptionValue(obj.cast<Identifier>());
    }
    if (py::isinstance<py::int_>(obj)) {
        return OptionValue(obj.cast<int64_t>());
    }
    if (py::isinstance<py::float_>(obj)) {
        return OptionValue(obj.cast<double>());
    }
    if (py::isinstance<py::str>(obj)) {
        return OptionValue(obj.cast<std::string>());
    }
    if (py::isinstance<py::dict>(obj)) {
        return OptionValue(pyobj_to_options(obj.cast<py::dict>()));
    }
    throw std::invalid_argument(
        "Unsupported Options value type; use bool, int, float, str, "
        "contrek.Identifier, or dict");
}

Options pyobj_to_options(const py::dict& d) {
    Options opts;
    for (const auto& item : d) {
        const std::string key = py::str(item.first);
        // Convenience: "versus" is always meant to be an Identifier
        // (Ruby symbol equivalent) in the C++ engine -- accept a plain
        // Python string here too, instead of requiring
        // contrek.Identifier("a") explicitly every time.
        if (key == "versus" && py::isinstance<py::str>(item.second)) {
            opts[key] = OptionValue(Identifier(item.second.cast<std::string>()));
        } else {
            opts[key] = pyobj_to_optionvalue(item.second);
        }
    }
    return opts;
}

// Reverse of pyobj_to_options(): C++ Options -> Python dict. Used to
// echo back ProcessResult::options (the options the engine actually used).
py::dict options_to_pydict(const Options& opts);  // forward decl (mutual recursion)

py::object optionvalue_to_pyobj(const OptionValue& v) {
    if (v.is_bool()) return py::bool_(v.as_bool());
    if (v.is_integer()) return py::int_(v.as_integer());
    if (v.is_double()) return py::float_(v.as_double());
    if (v.is_string()) return py::str(v.as_string());
    if (v.is_identifier()) return py::str(v.as_identifier().value);
    if (v.is_options()) return options_to_pydict(v.as_options());
    return py::none();
}

py::dict options_to_pydict(const Options& opts) {
    py::dict d;
    for (const auto& item : opts.values()) {
        d[py::str(item.first)] = optionvalue_to_pyobj(item.second);
    }
    return d;
}

// Reverse of points_to_ndarray(): parse a Python sequence of points
// into a vector<Point>. Accepts either [x, y] pairs (list/tuple/numpy
// row) or {"x": .., "y": ..} dicts, for convenience.
std::vector<Point> pyobj_to_points(const py::object& obj) {
    std::vector<Point> pts;
    for (const auto& item : obj) {
        if (py::isinstance<py::dict>(item)) {
            py::dict pd = py::reinterpret_borrow<py::dict>(item);
            pts.emplace_back(pd["x"].cast<int>(), pd["y"].cast<int>());
        } else {
            py::sequence seq = py::reinterpret_borrow<py::sequence>(item);
            pts.emplace_back(seq[0].cast<int>(), seq[1].cast<int>());
        }
    }
    return pts;
}

// Reverse of polygon_to_pydict(): build a real ::Polygon from a Python
// dict {"outer": [...], "inner": [[...], ...], "bounds": {...} (optional)}.
::Polygon pydict_to_polygon(const py::dict& d) {
    ::Polygon poly;
    poly.outer = pyobj_to_points(d["outer"]);
    if (d.contains("inner")) {
        for (const auto& ring : d["inner"]) {
            poly.inner.push_back(pyobj_to_points(py::reinterpret_borrow<py::object>(ring)));
        }
    }
    if (d.contains("bounds")) {
        py::dict b = d["bounds"];
        poly.bounds.min_x = b["min_x"].cast<int>();
        poly.bounds.max_x = b["max_x"].cast<int>();
        poly.bounds.min_y = b["min_y"].cast<int>();
        poly.bounds.max_y = b["max_y"].cast<int>();
    } else {
        RectBounds b = RectBounds::empty();
        for (const auto& p : poly.outer) b.expand(p.x, p.y);
        poly.bounds = b;
    }
    return poly;
}


template <typename PointSeq>
py::array_t<int32_t> points_to_ndarray(const PointSeq& pts) {
    const size_t n = pts.size();
    py::array_t<int32_t> arr({static_cast<py::ssize_t>(n), static_cast<py::ssize_t>(2)});
    auto buf = arr.mutable_unchecked<2>();
    size_t i = 0;
    for (const auto& p : pts) {
        buf(i, 0) = p.x;
        buf(i, 1) = p.y;
        ++i;
    }
    return arr;
}

// Convert ::ProcessResult::treemap (vector<pair<int,int>>) into
// an (N, 2) int32 ndarray, same shape convention as points.
py::array_t<int32_t> treemap_to_ndarray(const std::vector<std::pair<int, int>>& tm) {
    const size_t n = tm.size();
    py::array_t<int32_t> arr({static_cast<py::ssize_t>(n), static_cast<py::ssize_t>(2)});
    auto buf = arr.mutable_unchecked<2>();
    size_t i = 0;
    for (const auto& pr : tm) {
        buf(i, 0) = pr.first;
        buf(i, 1) = pr.second;
        ++i;
    }
    return arr;
}

py::dict polygon_to_pydict(const ::Polygon& poly) {
    py::dict d;
    d["outer"] = points_to_ndarray(poly.outer);

    py::list inner_list;
    for (const auto& ring : poly.inner) {
        inner_list.append(points_to_ndarray(ring));
    }
    d["inner"] = inner_list;

    py::dict bounds;
    bounds["min_x"] = poly.bounds.min_x;
    bounds["min_y"] = poly.bounds.min_y;
    bounds["max_x"] = poly.bounds.max_x;
    bounds["max_y"] = poly.bounds.max_y;
    bounds["is_empty"] = poly.bounds.is_empty();
    d["bounds"] = bounds;

    return d;
}

// Eagerly convert a ProcessResult into a plain Python dict. This keeps
// the Python-facing object simple (no lifetime coupling to TraceContext)
// at the cost of one extra copy -- fine given typical contour output
// sizes, and avoids exposing move-only C++ types to Python.
py::dict process_result_to_pydict(const ::ProcessResult& result) {
    py::dict out;
    out["groups"] = result.groups;
    out["width"] = result.width;
    out["height"] = result.height;
    out["versus"] = result.versus;
    out["has_bounds"] = result.has_bounds;
    out["named_sequence"] = result.named_sequence;
    out["number_of_threads"] = result.number_of_threads;

    py::dict benchmarks;
    for (const auto& kv : result.benchmarks) {
        benchmarks[py::str(kv.first)] = kv.second;
    }
    out["benchmarks"] = benchmarks;

    py::list polygons;
    for (const auto& poly : result.polygons) {
        polygons.append(polygon_to_pydict(poly));
    }
    out["polygons"] = polygons;

    out["treemap"] = treemap_to_ndarray(result.treemap);
    out["options"] = options_to_pydict(result.options);

    return out;
}

// Build a Matcher the same way Contrek::trace() does internally, so the
// low-level API stays consistent with the high-level one: target_color
// == -1 means "read the color at pixel (0, 0) of the given bitmap".
std::unique_ptr<Matcher> make_matcher(Bitmap& bitmap, int32_t target_color, Contrek::MatchMode mode) {
    int32_t color_to_match = target_color;
    if (color_to_match == -1) {
        // Auto-detect assumes a packed 4-byte-per-pixel (RGBA) layout,
        // valid for FastPngBitmap but NOT for a plain string-backed
        // Bitmap (typically 1 byte per character). Guard against
        // reading past the row buffer in that case.
        if (bitmap.get_bytes_per_pixel() != 4) {
            throw std::invalid_argument(
                "target_color=-1 (auto-detect) requires a 4-byte-per-pixel "
                "bitmap (e.g. FastPngBitmap); pass an explicit target_color "
                "for string-backed Bitmap instances.");
        }
        color_to_match = static_cast<int32_t>(
            *reinterpret_cast<const uint32_t*>(bitmap.get_row_ptr(0)));
    }
    if (mode == Contrek::MatchMode::NOT_COLOR) {
        return std::make_unique<RGBNotMatcher>(color_to_match);
    }
    return std::make_unique<RGBMatcher>(color_to_match);
}

}  // namespace

// --- Streaming API: keeps a real C++ ProcessResult alive across calls -
//
// find_polygons()/trace() convert straight to a Python dict, which loses
// the native ProcessResult -- fine for one-shot use, but
// StreamingMerger::add_tile() needs a real, mutable ProcessResult& (it
// consumes/streams polygons out of it tile by tile). RawProcessResult is
// an opaque handle around the native object so it can be threaded
// through find_polygons_raw() -> merger.add_tile() without a premature
// conversion to Python types.
class RawProcessResult {
 public:
    explicit RawProcessResult(std::unique_ptr<::ProcessResult> ptr) : ptr_(std::move(ptr)) {}
    ::ProcessResult& get() { return *ptr_; }
    py::dict to_dict() const { return process_result_to_pydict(*ptr_); }

 private:
    std::unique_ptr<::ProcessResult> ptr_;
};

// Build a synthetic RawProcessResult directly from ready-made polygon
// data, bypassing PolygonFinder/Bitmap entirely -- mirrors the Ruby
// "merge mode from existing polygons" pattern, where hand-built
// polygons are fed straight into a merger.
RawProcessResult make_result_from_polygons(py::list polygons_in, int width, int height) {
    auto result = std::make_unique<::ProcessResult>();
    result->width = width;
    result->height = height;
    for (const auto& item : polygons_in) {
        py::dict pd = py::reinterpret_borrow<py::dict>(item);
        result->polygons.push_back(pydict_to_polygon(pd));
    }
    result->groups = static_cast<int>(result->polygons.size());
    result->has_bounds = true;
    return RawProcessResult(std::move(result));
}

// Custom streambuf C++ che reindirizza le scritture al metodo .write() di un oggetto Python
class PyStreamBuffer : public std::streambuf {
public:
    explicit PyStreamBuffer(py::object py_stream) : py_stream_(py_stream) {}

protected:
    virtual std::streamsize xsputn(const char* s, std::streamsize n) override {
        py::gil_scoped_acquire acquire;
        py_stream_.attr("write")(py::str(s, n));
        return n;
    }

    virtual int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) {
            char c = static_cast<char>(ch);
            py::gil_scoped_acquire acquire;
            py_stream_.attr("write")(py::str(&c, 1));
            return ch;
        }
        return traits_type::eof();
    }

private:
    py::object py_stream_;
};

// Owns the std::ofstream so Python callers just pass a file path --
// SvgStreamingMerger/GeoJsonStreamingMerger only take a raw
// std::ofstream*, they don't own or open it themselves.
class PySvgStreamingMerger {
 public:
    PySvgStreamingMerger(py::dict options, const std::string& output_path,
                          int width, int height)
        : ofs_(output_path), merger_(0, pyobj_to_options(options), &ofs_, width, height) {
        if (!ofs_.is_open()) {
            throw std::runtime_error("Unable to open output file: " + output_path);
        }
    }

    void add_tile(RawProcessResult& tile, bool flush = false) {
        py::gil_scoped_release release;
        merger_.add_tile(tile.get(), flush);
    }

    py::dict process_info() {
        ::ProcessResult* raw;
        {
            py::gil_scoped_release release;
            raw = merger_.process_info();
        }
        finalize_stream();
        std::unique_ptr<::ProcessResult> owned(raw);
        return process_result_to_pydict(*owned);
    }

 private:
    // Effettua il flush e chiude esplicitamente lo stream C++ per garantire
    // la scrittura completa di </svg> sul file prima della rilettura
    void finalize_stream() {
        if (ofs_.is_open()) {
            ofs_.flush();
            ofs_.close();
        }
    }
    std::ofstream ofs_;
    SvgStreamingMerger merger_;
};

class PyGeoJsonStreamingMerger {
 public:
    PyGeoJsonStreamingMerger(py::dict options, const std::string& output_path,
                              uint32_t pixel_value)
        : ofs_(output_path), merger_(0, pyobj_to_options(options), &ofs_, pixel_value) {
        if (!ofs_.is_open()) {
            throw std::runtime_error("Unable to open output file: " + output_path);
        }
    }

    void add_tile(RawProcessResult& tile, bool flush = false) {
        py::gil_scoped_release release;
        merger_.add_tile(tile.get(), flush);
    }

    py::dict process_info() {
        ::ProcessResult* raw;
        {
            py::gil_scoped_release release;
            raw = merger_.process_info();
        }
        finalize_stream();
        std::unique_ptr<::ProcessResult> owned(raw);
        return process_result_to_pydict(*owned);
    }

 private:
    // Effettua il flush e chiude esplicitamente lo stream C++ per garantire
    // la scrittura completa di </svg> sul file prima della rilettura
    void finalize_stream() {
        if (ofs_.is_open()) {
            ofs_.flush();
            ofs_.close();
        }
    }
    std::ofstream ofs_;
    GeoJsonStreamingMerger merger_;
};

// In-memory tile merge (no file output) -- e.g. for combining tiles from
// find_polygons_raw() into one ProcessResult without going through the
// SVG/GeoJSON streaming path.
class PyVerticalMerger {
 public:
    PyVerticalMerger(int number_of_threads, py::dict options)
        : merger_(number_of_threads, pyobj_to_options(options)) {}

    void add_tile(RawProcessResult& tile) {
        py::gil_scoped_release release;
        merger_.add_tile(tile.get());
    }

    py::dict process_info() {
        ::ProcessResult* raw;
        {
            py::gil_scoped_release release;
            raw = merger_.process_info();
        }
        std::unique_ptr<::ProcessResult> owned(raw);
        return process_result_to_pydict(*owned);
    }

 private:
    VerticalMerger merger_;
};


// Same idea as PyVerticalMerger, but stitching tiles horizontally
// instead of vertically.
class PyHorizontalMerger {
 public:
    PyHorizontalMerger(int number_of_threads, py::dict options)
        : merger_(number_of_threads, pyobj_to_options(options)) {}

    void add_tile(RawProcessResult& tile) {
        py::gil_scoped_release release;
        merger_.add_tile(tile.get());
    }

    py::dict process_info() {
        ::ProcessResult* raw;
        {
            py::gil_scoped_release release;
            raw = merger_.process_info();
        }
        std::unique_ptr<::ProcessResult> owned(raw);
        return process_result_to_pydict(*owned);
    }

 private:
    HorizontalMerger merger_;
};



PYBIND11_MODULE(_contrek, m) {
    m.doc() = "Low-level pybind11 bindings for the Contrek C++ core";

    py::enum_<Contrek::Versus>(m, "Versus")
        .value("ANTICLOCKWISE", Contrek::Versus::A)
        .value("CLOCKWISE", Contrek::Versus::O);

    m.attr("NODE_VERSUS_O") = static_cast<int>(Node::O);
    m.attr("NODE_VERSUS_A") = static_cast<int>(Node::A);

    py::enum_<Contrek::MatchMode>(m, "MatchMode")
        .value("NOT_COLOR", Contrek::MatchMode::NOT_COLOR)
        .value("EXACT_COLOR", Contrek::MatchMode::EXACT_COLOR);

    py::enum_<Contrek::Connectivity>(m, "Connectivity")
        .value("ORTHOGONAL", Contrek::Connectivity::ORTHOGONAL)
        .value("OMNIDIRECTIONAL", Contrek::Connectivity::OMNIDIRECTIONAL);

    py::class_<Contrek::Config>(m, "Config")
        .def(py::init<>())
        .def_readwrite("number_of_threads", &Contrek::Config::threads)
        .def_readwrite("number_of_tiles", &Contrek::Config::tiles)
        .def_readwrite("versus", &Contrek::Config::versus)
        .def_readwrite("compress_unique", &Contrek::Config::compress_unique)
        .def_readwrite("compress_linear", &Contrek::Config::compress_linear)
        .def_readwrite("compress_raster", &Contrek::Config::compress_raster)
        .def_readwrite("compress_douglas_peucker", &Contrek::Config::compress_douglas_peucker)
        .def_readwrite("compress_visvalingam", &Contrek::Config::compress_visvalingam)
        .def_readwrite("compress_visvalingam_tolerance", &Contrek::Config::compress_visvalingam_tolerance)
        .def_readwrite("treemap", &Contrek::Config::treemap)
        .def_readwrite("bounds", &Contrek::Config::bounds)
        .def_readwrite("named_sequences", &Contrek::Config::named_sequences)
        .def_readwrite("unsafe_mode", &Contrek::Config::unsafe_mode)
        .def_readwrite("deterministic", &Contrek::Config::deterministic)
        .def_readwrite("target_color", &Contrek::Config::target_color)
        .def_readwrite("mode", &Contrek::Config::mode)
        .def_readwrite("connectivity_mode", &Contrek::Config::connectivity_mode);

    m.def(
        "trace",
        [](const std::string& image_path, const Contrek::Config& cfg) {
            // Release the GIL while the C++ engine runs its worker
            // threads. This is the main advantage over the Ruby MRI
            // wrapper (GIL-serialized in pure-Ruby code paths): Contrek's
            // native threads run at full speed here.
            Contrek::TraceContext ctx = [&] {
                py::gil_scoped_release release;
                return Contrek::trace(image_path, cfg);
            }();

            // ctx.result is a unique_ptr<ProcessResult>; ctx itself goes
            // out of scope after this call, so we must extract data,
            // not references, before returning.
            return process_result_to_pydict(*ctx.result);
        },
        py::arg("image_path"),
        py::arg("config") = Contrek::Config{},
        R"doc(
            Trace polygon contours from a raster image using the Contrek engine.

            Returns a dict with keys:
              groups (int), width (int), height (int), versus (int),
              has_bounds (bool), named_sequence (str),
              benchmarks (dict[str, float]),
              treemap (numpy.ndarray[int32], shape (N, 2)),
              polygons (list[dict]) -- each with:
                outer (numpy.ndarray[int32], shape (N, 2)),
                inner (list[numpy.ndarray[int32]]),
                bounds (dict: min_x, min_y, max_x, max_y)
        )doc"
    );

    // ----- Low-level API: direct Bitmap / PolygonFinder access --------
    //
    // These mirror the lower-level Ruby API used in tests like
    // "progressive merging on disk with compression": constructing a
    // Bitmap directly from an in-memory pattern string (instead of
    // reading a PNG from disk), and calling PolygonFinder.process_info()
    // directly instead of going through Contrek::trace().

    py::class_<Identifier>(m, "Identifier")
        .def(py::init<std::string>(), py::arg("value"),
             "Mirrors a Ruby symbol (e.g. :a) when building low-level Options dicts.")
        .def(py::self == py::self)
        .def_readwrite("value", &Identifier::value)
        .def("__repr__", [](const Identifier& id) { return "Identifier('" + id.value + "')"; });

    py::class_<Bitmap>(m, "Bitmap")
        .def(py::init<std::string, int>(), py::arg("data"), py::arg("width"),
             R"doc(
                Build a bitmap directly from an in-memory pattern string,
                e.g. a small ASCII-art pattern used for tests, bypassing
                PNG decoding entirely. `width` is the row stride (number
                of characters per row) used to interpret `data` as a 2D grid.
             )doc")
        .def("w", &Bitmap::w)
        .def("h", &Bitmap::h)
        .def("error", &Bitmap::error);

    py::class_<FastPngBitmap, Bitmap>(m, "FastPngBitmap")
        .def(py::init<std::string>(), py::arg("path"),
             "Load and decode a PNG file from disk.");

    m.def(
        "find_polygons",
        [](Bitmap& bitmap, py::dict options, int32_t target_color, Contrek::MatchMode mode,
           Bitmap* test_bitmap, int start_x, int end_x, int number_of_threads) {
            Options cpp_options = pyobj_to_options(options);
            auto matcher = make_matcher(bitmap, target_color, mode);
            bool wants_concurrent = number_of_threads > 0;

            py::dict out;
            py::gil_scoped_release release;
            if (wants_concurrent) {
                Finder finder(number_of_threads, &bitmap, matcher.get(), cpp_options);
                std::unique_ptr<::ProcessResult> raw(finder.process_info());
                py::gil_scoped_acquire acquire;
                out = process_result_to_pydict(*raw);
            } else {
                PolygonFinder finder(&bitmap, matcher.get(), test_bitmap, cpp_options, start_x, end_x);
                std::unique_ptr<::ProcessResult> raw(finder.process_info());
                py::gil_scoped_acquire acquire;
                out = process_result_to_pydict(*raw);
            }
            return out;
        },
        py::arg("bitmap"),
        py::arg("options") = py::dict(),
        py::arg("target_color") = -1,
        py::arg("mode") = Contrek::MatchMode::NOT_COLOR,
        py::arg("test_bitmap") = nullptr,
        py::arg("start_x") = 0,
        py::arg("end_x") = -1,
        py::arg("number_of_threads") = 0,
        R"doc(
            Low-level polygon finding on an already-constructed Bitmap.

            Uses the real multi-threaded Finder (tiles + number_of_threads) if
            number_of_threads > 1 or options contains "number_of_tiles"; otherwise
            falls back to the single-threaded PolygonFinder, mirroring
            Ruby's Contrek.contour! routing behavior.

            target_color == -1 (default) auto-detects from the bitmap's
            pixel (0, 0), same convention as Contrek::trace().

            Returns the same dict shape as trace().
        )doc"
    );

    // ----- Streaming API: SVG / GeoJSON progressive merge on disk -----
    //
    // Mirrors the Ruby streaming test pattern: run find_polygons_raw()
    // per tile, feed each result into add_tile() (with flush=True on
    // the last tile), then call process_info() for final metadata.
    // Output is written incrementally to the given file path.

    py::class_<RawProcessResult>(m, "RawProcessResult")
        .def("to_dict", &RawProcessResult::to_dict,
             "Convert to the same plain dict shape returned by trace()/find_polygons().");

    m.def(
        "find_polygons_raw",
        [](Bitmap& bitmap, py::dict options, int32_t target_color, Contrek::MatchMode mode,
           Bitmap* test_bitmap, int start_x, int end_x, int number_of_threads) {
            Options cpp_options = pyobj_to_options(options);
            auto matcher = make_matcher(bitmap, target_color, mode);

            bool wants_concurrent = number_of_threads > 0;

            ::ProcessResult* raw;
            {
                py::gil_scoped_release release;
                if (wants_concurrent) {
                    Finder finder(number_of_threads, &bitmap, matcher.get(), cpp_options);
                    raw = finder.process_info();
                } else {
                    PolygonFinder finder(&bitmap, matcher.get(), test_bitmap, cpp_options, start_x, end_x);
                    raw = finder.process_info();
                }
            }
            return RawProcessResult(std::unique_ptr<::ProcessResult>(raw));
        },
        py::arg("bitmap"),
        py::arg("options") = py::dict(),
        py::arg("target_color") = -1,
        py::arg("mode") = Contrek::MatchMode::NOT_COLOR,
        py::arg("test_bitmap") = nullptr,
        py::arg("start_x") = 0,
        py::arg("end_x") = -1,
        py::arg("number_of_threads") = 0,
        R"doc(
            Same as find_polygons(), but returns a RawProcessResult handle
            instead of a dict -- use this when feeding tiles into
            SvgStreamingMerger / GeoJsonStreamingMerger via add_tile().

            Uses the real multi-threaded Finder if number_of_threads > 1 or options
            contains "number_of_tiles"; otherwise falls back to
            PolygonFinder (single-threaded).
        )doc"
    );

    m.def(
        "make_result_from_polygons",
        &make_result_from_polygons,
        py::arg("polygons"), py::arg("width"), py::arg("height"),
        R"doc(
            Build a RawProcessResult directly from ready-made polygon data,
            bypassing bitmap tracing entirely -- for feeding hand-built or
            externally-computed polygons into a merger (VerticalMerger,
            HorizontalMerger, SvgStreamingMerger, GeoJsonStreamingMerger).

            polygons: list of dicts, each {"outer": [[x,y], ...] or
            [{"x":.., "y":..}, ...], "inner": [[...], ...] (optional),
            "bounds": {"min_x":.., "max_x":.., "min_y":.., "max_y":..}
            (optional -- auto-computed from outer if omitted)}.
        )doc"
    );

    py::class_<PySvgStreamingMerger>(m, "SvgStreamingMerger")
        .def(py::init<py::dict, std::string, int, int>(),
             py::arg("options"), py::arg("output_path"),
             py::arg("width"), py::arg("height"),
             "Streams polygons to an SVG file incrementally as tiles are added.")
        .def("add_tile", &PySvgStreamingMerger::add_tile,
             py::arg("tile"), py::arg("flush") = false,
             "Feed one tile's RawProcessResult. Pass flush=True on the last tile.")
        .def("process_info", &PySvgStreamingMerger::process_info,
             "Finalize the stream and return summary metadata as a dict.");

    py::class_<PyGeoJsonStreamingMerger>(m, "GeoJsonStreamingMerger")
        .def(py::init<py::dict, std::string, uint32_t>(),
             py::arg("options"), py::arg("output_path"),
             py::arg("pixel_value"),
             "Streams polygons to a GeoJSON file incrementally as tiles are added.")
        .def("add_tile", &PyGeoJsonStreamingMerger::add_tile,
             py::arg("tile"), py::arg("flush") = false,
             "Feed one tile's RawProcessResult. Pass flush=True on the last tile.")
        .def("process_info", &PyGeoJsonStreamingMerger::process_info,
             "Finalize the stream and return summary metadata as a dict.");

    py::class_<PyVerticalMerger>(m, "VerticalMerger")
        .def(py::init<int, py::dict>(),
             py::arg("number_of_threads") = 0, py::arg("options") = py::dict(),
             "In-memory tile merge (no file output) -- combines RawProcessResult "
             "tiles (e.g. from find_polygons_raw()) into one merged result.")
        .def("add_tile", &PyVerticalMerger::add_tile, py::arg("tile"),
             "Feed one tile's RawProcessResult into the merge.")
        .def("process_info", &PyVerticalMerger::process_info,
             "Finalize the merge and return the combined result as a dict.");

    py::class_<PyHorizontalMerger>(m, "HorizontalMerger")
        .def(py::init<int, py::dict>(),
             py::arg("number_of_threads") = 0, py::arg("options") = py::dict(),
             "In-memory tile merge (no file output), stitching tiles "
             "horizontally instead of vertically.")
        .def("add_tile", &PyHorizontalMerger::add_tile, py::arg("tile"),
             "Feed one tile's RawProcessResult into the merge.")
        .def("process_info", &PyHorizontalMerger::process_info,
             "Finalize the merge and return the combined result as a dict.");
}
