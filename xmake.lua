set_project("hypercube")
set_version("0.1.0")
set_license("MIT")
set_languages("cxx20")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_requires("spdlog 1.17", "nlohmann_json 3.12", "json-schema-validator 2.4", "argparse 3.2", "catch2 3.14")

target("core")
    set_kind("static")
    add_packages("spdlog", "nlohmann_json", "json-schema-validator", {public = true})
    add_files("src/core/*.cpp")
    add_includedirs("src", {public = true})
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end

target("cli")
    set_kind("binary")
    set_rundir(".")
    add_packages("argparse")
    add_deps("core")
    add_files("src/main.cpp")

target("test")
    set_kind("binary")
    set_rundir(".")
    add_deps("core")
    add_packages("catch2")
    add_files("tests/*.cpp")

target("report")
    set_kind("binary")
    set_rundir(".")
    add_deps("core")
    add_files("report/*.cpp")
