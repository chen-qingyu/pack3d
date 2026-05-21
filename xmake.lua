set_project("hypercube")
set_version("0.1.0")
set_license("MIT")
set_languages("cxx20")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_requires("nlohmann_json 3.12", "spdlog 1.17", "catch2 3.14", "argparse 3.2")

target("hypercube")
    set_kind("binary")
    set_rundir(".")
    add_files("src/main.cpp")
