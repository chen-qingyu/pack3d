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
        s.success = false;
        s.reason = reason::k_invalid_range;
        s.violations = std::move(schema_errors);
        return solution_to_json(s);
    }

    auto problem = problem_from_json(j);
    if (!problem.has_value())
    {
        Solution s;
        s.success = false;
        s.reason = reason::k_invalid_range;
        s.violations.push_back({"json_parse", {}, "failed_to_parse_json_structure"});
        return solution_to_json(s);
    }

    // 语义预校验
    auto violations = pre_validate_input(problem.value());
    if (!violations.empty())
    {
        Solution s;
        s.success = false;
        s.reason = violations.empty() ? reason::k_invalid_range : violations[0].details;
        s.violations = std::move(violations);
        return solution_to_json(s);
    }

    SolverEngine engine(problem.value());
    Solution solution = engine.solve();
    return solution_to_json(solution);
}

} // namespace hypercube
