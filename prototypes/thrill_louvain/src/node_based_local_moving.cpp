#if defined(FIXED_RATIO)
  #define MAX_ITERATIONS 8 * FIXED_RATIO
#else
  #define MAX_ITERATIONS 32
#endif

#include "thrill_local_moving.hpp"
#include "thrill_louvain.hpp"


int main(int argc, char const *argv[]) {
  return Louvain::performAndEvaluate(argc, argv, "thrill node based fully distributed local moving", [](const auto& graph, Logging::Id logging_id) {
    return Louvain::louvain(graph, logging_id, [](const auto& graph, Logging::Id level_logging_id) {
      return LocalMoving::distributedLocalMoving(graph, MAX_ITERATIONS, level_logging_id);
    });
  });
}

