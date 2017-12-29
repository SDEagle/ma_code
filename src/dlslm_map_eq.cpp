#if defined(FIXED_RATIO)
  #define MAX_ITERATIONS 8 * FIXED_RATIO
#else
  #define MAX_ITERATIONS 32
#endif

#include "algo/thrill/synchronous_map_equation.hpp"
#include "algo/thrill/louvain.hpp"


int main(int argc, char const *argv[]) {
  std::cout << "INSTRSET: " << INSTRSET << std::endl;

  return Louvain::performAndEvaluate(argc, argv, "synchronous local moving with map equation", [](const auto& graph, Logging::Id logging_id, uint32_t seed) {
    return Louvain::louvain(graph, logging_id, seed, [](const auto& graph, uint32_t seed, Logging::Id level_logging_id) {
      return LocalMoving::distributedLocalMoving(graph, MAX_ITERATIONS, seed, level_logging_id);
    });
  });
}

