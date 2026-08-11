#pragma once

#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "core/app.hpp"
#include "core/io.hpp"
#include "core/types.hpp"

using namespace pack3d;

// 进程启动即全局关闭 spdlog，避免 Catch2 随机测试顺序导致日志时有时无
inline const bool g_spdlog_off = (spdlog::set_level(spdlog::level::off), true);

// 从 JSON 文件加载测试场景
inline json load_data(const char* path)
{
    std::ifstream ifs(path);
    REQUIRE(ifs.is_open());
    std::stringstream buf;
    buf << ifs.rdbuf();
    return json::parse(buf.str());
}
