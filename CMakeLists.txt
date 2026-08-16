cmake_minimum_required(VERSION 3.18)
project(contrek_python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- pybind11 ------------------------------------------------------------
find_package(pybind11 CONFIG REQUIRED)

# --- Contrek core (AGPLv3) --------------------------------------------
# The whole upstream repo is vendored as a git submodule at vendor/contrek
# (a submodule can only point at a repo root, not a subfolder), so we
# add_subdirectory() into its PolygonFinder folder from there.
# See: https://github.com/runout77/contrek/tree/main/ext/cpp_polygon_finder/PolygonFinder
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/contrek/ext/cpp_polygon_finder/PolygonFinder)

# --- Python extension module ----------------------------------------------
pybind11_add_module(_contrek MODULE src/bindings.cpp)
target_link_libraries(_contrek PRIVATE ContrekLib)

# Release the GIL by default inside long-running C++ calls; actual
# py::gil_scoped_release calls happen explicitly in bindings.cpp.
target_compile_definitions(_contrek PRIVATE VERSION_INFO="${PROJECT_VERSION}")

install(TARGETS _contrek LIBRARY DESTINATION contrek)
