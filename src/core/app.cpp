#include "app.hpp"

#include <string>
#include <variant>

#include "io.hpp"
#include "solver.hpp"

namespace hypercube
{

json run_solver(const std::string& json_input) noexcept
{
    auto parsed = parse_json(json_input);

    if (std::holds_alternative<Solution>(parsed))
    {
        return solution_to_json(std::get<Solution>(std::move(parsed)));
    }

    Problem problem = std::get<Problem>(std::move(parsed));

    SolverEngine engine(problem);
    Solution solution = engine.solve();

    return solution_to_json(solution);
}

} // namespace hypercube
