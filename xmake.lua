set_project("pack3d")
set_version("0.1.0")
set_license("MIT")
set_languages("cxx20")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_defines("FMT_HEADER_ONLY") -- spdlog bundled fmt 默认非 header-only：MSVC 可传递解析，GCC 链接会缺
add_requires("spdlog 1.17", "nlohmann_json 3.12", "json-schema-validator 2.4", "argparse 3.2", "catch2 3.14", "pybind11 3.0", "magic_enum 0.9")
if is_plat("linux") then
    add_requireconfs("pybind11.python", {override = true, configs = {headeronly = true}})
end

-- 将 data/input_schema.json 嵌入为 C++ 头文件，消除运行时路径依赖
rule("embed_schema")
    on_load(function (target)
        local schema_path = path.join(os.projectdir(), "data/input_schema.json")
        local header_path = path.join(target:autogendir(), "input_schema.h")
        target:add("includedirs", target:autogendir())
        -- 仅在 input_schema.json 文件发生变化时才重新生成头文件
        if os.isfile(header_path) and os.mtime(header_path) >= os.mtime(schema_path) then
            return
        end
        io.writefile(header_path, string.format([[
#pragma once
#include <string_view>
namespace pack3d { inline constexpr std::string_view INPUT_SCHEMA = R"(%s)"; }
]], io.readfile(schema_path)))
    end)

target("core")
    set_kind("static")
    add_rules("embed_schema")
    add_packages("spdlog", "nlohmann_json", "json-schema-validator", "magic_enum", {public = true})
    add_files("src/core/**.cpp")
    add_headerfiles("src/core/**.hpp")
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end

target("cli")
    set_kind("binary")
    set_rundir(".")
    add_packages("argparse")
    add_deps("core")
    add_files("src/main.cpp")

target("lib")
    add_rules("python.module")
    add_deps("core")
    add_packages("pybind11")
    add_files("src/python_module.cpp")

target("test")
    set_kind("binary")
    set_rundir(".")
    add_deps("core")
    add_packages("catch2")
    add_files("tests/*.cpp")
    add_includedirs("src")

target("report")
    set_kind("binary")
    set_rundir(".")
    add_packages("argparse")
    add_deps("core")
    add_files("report/*.cpp")
    add_includedirs("src")
