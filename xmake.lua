set_project("hypercube")
set_version("0.1.0")
set_license("MIT")
set_languages("cxx20")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_requires("nlohmann_json 3.12", "spdlog 1.17", "catch2 3.14", "argparse 3.2", "json-schema-validator 2.4")

target("hypercube-lib")
    set_kind("static")
    add_packages("nlohmann_json", "spdlog", "json-schema-validator", {public = true})
    add_files("src/core/*.cpp")
    add_includedirs("src", {public = true})
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end

target("hypercube")
    set_kind("binary")
    set_rundir(".")
    add_packages("argparse")
    add_deps("hypercube-lib")
    add_files("src/main.cpp")

target("test")
    set_kind("binary")
    set_rundir(".")
    add_deps("hypercube-lib")
    add_packages("catch2")
    add_files("tests/*.cpp")

target("report")
    set_kind("binary")
    set_rundir(".")
    add_deps("hypercube-lib")
    add_packages("nlohmann_json", "spdlog", {public = true})
    add_files("report/*.cpp")
