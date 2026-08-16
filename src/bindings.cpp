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
//  - RectBounds fields are assumed to be {min_x, min_y, max_x, max_y}
//    (only min_x/max_x confirmed from ProcessResult::translate()).
//    VERIFY against the real RectBounds definition and adjust the
//    bounds block in polygon_to_pydict() below if field names differ.

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "ContrekApi.h"

namespace py = pybind11;

namespace {

// Convert a vector/list of Contrek::Point into an (N, 2) int32 ndarray.
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

// Convert Contrek::ProcessResult::treemap (vector<pair<int,int>>) into
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

    // --- RectBounds: field names ASSUMED, verify against real header ---
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
py::dict process_result_to_pydict(const ProcessResult& result) {
    py::dict out;
    out["groups"] = result.groups;
    out["width"] = result.width;
    out["height"] = result.height;
    out["versus"] = result.versus;
    out["has_bounds"] = result.has_bounds;
    out["named_sequence"] = result.named_sequence;

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

    return out;
}

}  // namespace

PYBIND11_MODULE(_contrek, m) {
    m.doc() = "Low-level pybind11 bindings for the Contrek C++ core";

    py::enum_<Contrek::Versus>(m, "Versus")
        .value("ANTICLOCKWISE", Contrek::Versus::A)
        .value("CLOCKWISE", Contrek::Versus::O);

    py::enum_<Contrek::MatchMode>(m, "MatchMode")
        .value("NOT_COLOR", Contrek::MatchMode::NOT_COLOR)
        .value("EXACT_COLOR", Contrek::MatchMode::EXACT_COLOR);

    py::enum_<Contrek::Connectivity>(m, "Connectivity")
        .value("ORTHOGONAL", Contrek::Connectivity::ORTHOGONAL)
        .value("OMNIDIRECTIONAL", Contrek::Connectivity::OMNIDIRECTIONAL);

    py::class_<Contrek::Config>(m, "Config")
        .def(py::init<>())
        .def_readwrite("threads", &Contrek::Config::threads)
        .def_readwrite("tiles", &Contrek::Config::tiles)
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
}
