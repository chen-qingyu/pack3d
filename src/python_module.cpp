#include <pybind11/pybind11.h>

#include "core/app.hpp"

namespace py = pybind11;

PYBIND11_MODULE(lib, module)
{
    module.doc() = "Python SDK for pack3d solver.";
    module.def("run", &pack3d::run, py::arg("input"), "Run the pack3d solver.");
}
