#pragma once

#include <vector>
#include <unordered_map>
#include <algorithm>

#include <routingkit/bit_vector.h>
#include <routingkit/id_mapper.h>
#include "data/thrill/graph.hpp"

class GhostClusterStore {
private:

  std::vector<ClusterId> clusters;

public:

  GhostClusterStore(const size_t node_count, const ClusterId initial_cluster_id = 0) :
    clusters(node_count, initial_cluster_id) {}

  inline void set(const NodeId node, const ClusterId cluster_id) {
    clusters[node] = cluster_id;
  }

  inline ClusterId operator[](const NodeId node) const {
    if (node >= clusters.size()) {
      return node;
    }
    return clusters[node];
  }

  void assignSingletonClusterIds() {
    std::iota(clusters.begin(), clusters.end(), 0);
  }

  template<class Node>
  void rewriteClusterIds(const std::vector<Node>& id_space) {
    size_t id_counter = 0;
    std::unordered_map<NodeId, ClusterId> old_to_new;

    for (ClusterId& cluster_id : clusters) {
      if (old_to_new.find(cluster_id) == old_to_new.end()) {
        old_to_new[cluster_id] = id_space[id_counter++].id;
      }
      ClusterId new_id = old_to_new[cluster_id];
      cluster_id = new_id;
    }
  }

  ClusterId rewriteClusterIds(ClusterId id_counter = 0) {
    RoutingKit::BitVector vector(size());

    for (ClusterId cluster_id : clusters) {
      vector.set(cluster_id);
    }
    RoutingKit::LocalIDMapper id_mapper(vector);
    for (ClusterId& cluster_id : clusters) {
      cluster_id = id_counter + id_mapper.to_local(cluster_id);
    }

    return id_counter + id_mapper.local_id_count();
  }

  size_t size() const {
    return clusters.size();
  }
};
