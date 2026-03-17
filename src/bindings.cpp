#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "UsdSimApp.h"
#include "UsdDocument.h"

namespace py = pybind11;

PYBIND11_MODULE(pyusdSim, m) {
    m.doc() = "Python bindings for usdSim Qt Quick application";

    py::class_<UsdDocument>(m, "UsdDocument")
        .def("open", &UsdDocument::open)
        .def("open_from_stage_cache", &UsdDocument::openFromStageCache)
        .def("insert_to_stage_cache", &UsdDocument::insertToStageCache)
        .def("notify_stage_modified", [](UsdDocument &self, std::vector<std::string> paths) {
            QStringList qpaths;
            for (auto &p : paths) qpaths << QString::fromStdString(p);
            self.notifyStageModified(qpaths);
        }, py::arg("modified_prim_paths") = std::vector<std::string>{})
        .def("is_open", &UsdDocument::isOpen)
        .def("file_path", [](const UsdDocument &self) {
            return self.filePath().toStdString();
        });

    py::class_<UsdSimApp>(m, "UsdSimApp")
        .def(py::init<>())
        .def("register_types", &UsdSimApp::register_types)
        .def("init", [](UsdSimApp &self, std::vector<std::string> args) {
            std::vector<char*> argv;
            for (auto &a : args)
                argv.push_back(a.data());
            argv.push_back(nullptr);
            int argc = static_cast<int>(args.size());
            self.init(argc, argv.data());
        }, py::arg("args") = std::vector<std::string>{"usdSim"})
        .def("exec", &UsdSimApp::exec_app)
        .def("find_document", &UsdSimApp::findDocument, py::return_value_policy::reference)
        .def("process_events", &UsdSimApp::processEvents)
        .def("unregister_types", &UsdSimApp::unregister_types)
        .def("uninit", &UsdSimApp::uninit);
}
