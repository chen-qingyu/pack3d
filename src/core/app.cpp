#include "app.hpp"

#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "io.hpp"
#include "solver.hpp"

namespace hypercube
{

json run(const json& j) noexcept
{
    // schema 校验
    auto schema_errors = validate_schema(j);
    if (!schema_errors.empty())
    {
        spdlog::error("Input schema validation failed");
        Solution s;
        s.status = SolveStatus::Invalid;
        s.violations = std::move(schema_errors);
        return json(s);
    }

    auto problem = j.get<Problem>();

    // 语义校验
    auto violations = pre_validate_input(problem);
    if (!violations.empty())
    {
        spdlog::error("Input validation failed");
        Solution s;
        s.status = SolveStatus::Invalid;
        s.violations = std::move(violations);
        return json(s);
    }

    SolverEngine engine(problem);
    Solution solution = engine.solve();
    return json(solution);
}

} // namespace hypercube
