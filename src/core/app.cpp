#include "app.hpp"

#include <string>
#include <variant>

#include "constraints.hpp"
#include "io.hpp"
#include "solver.hpp"

namespace hypercube
{

nlohmann::json run_solver(const std::string& json_input, bool debug) noexcept
{
    auto parsed = parse_json(json_input);

    if (std::holds_alternative<Solution>(parsed))
    {
        Solution error_solution = std::get<Solution>(std::move(parsed));
        if (!debug)
        {
            error_solution.violations.clear();
        }
        return solution_to_json(error_solution);
    }

    Problem problem = std::get<Problem>(std::move(parsed));

    SolverEngine engine(problem);
    Solution solution = engine.solve();

    if (!debug)
    {
        solution.violations.clear();
    }

    return solution_to_json(solution);
}

} // namespace hypercube
