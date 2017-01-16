#include <thrill/api/print.hpp>
#include <thrill/api/read_lines.hpp>
#include <thrill/api/write_lines.hpp>
#include <thrill/api/sort.hpp>
#include <thrill/api/all_reduce.hpp>
#include <thrill/api/cache.hpp>
#include <thrill/api/collapse.hpp>
#include <thrill/api/reduce_by_key.hpp>
#include <thrill/api/zip_with_index.hpp>
#include <thrill/api/zip.hpp>
#include <thrill/api/group_by_key.hpp>
#include <thrill/api/print.hpp>
#include <thrill/api/size.hpp>
#include <thrill/api/reduce_to_index.hpp>
#include <thrill/api/sum.hpp>
#include <thrill/api/join.hpp>

#include <ostream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <set>
#include <map>

#include "graph.hpp"
#include "modularity.hpp"
#include "cluster_store.hpp"

struct Node {
  uint32_t id, degree;
};

struct NodeInfo {
  uint32_t id, data;
};

struct Edge {
  uint32_t tail, head;

  uint32_t getWeight() const {
    return 1;
  }
};

struct WeightedEdge {
  uint32_t tail, head, weight;

  uint32_t getWeight() const {
    return weight;
  }
};

std::ostream& operator << (std::ostream& os, Edge& e) {
  return os << e.tail << " -> " << e.head;
}

std::ostream& operator << (std::ostream& os, WeightedEdge& e) {
  return os << e.tail << " -> " << e.head << " (" << e.weight << ")";
}

std::ostream& operator << (std::ostream& os, NodeInfo& node_info) {
  return os << node_info.id << ": " << node_info.data;
}

template<class EdgeType>
thrill::DIA<NodeInfo> louvain(thrill::DIA<EdgeType>& edge_list) {
  auto nodes = edge_list
    .Map([](const EdgeType & edge) { return Node { edge.tail, 1 }; })
    .ReduceByKey(
      [](const Node & node) { return node.id; },
      [](const Node & node1, const Node & node2) { return Node { node1.id, node1.degree + node2.degree }; });

  const uint64_t node_count = nodes.Size();
  const uint64_t total_weight = edge_list.Keep().Map([](const EdgeType & edge) { return edge.getWeight(); }).Sum() / 2;

  auto node_partitions = nodes
    // Map to degree times partition
    .Map([](const Node & node) { return std::make_pair(node.id, 0u); });

  auto bloated_node_clusters = edge_list
    .InnerJoinWith(node_partitions,
      [](const EdgeType& edge) { return edge.tail; },
      [](const std::pair<uint32_t, uint32_t>& node_partition) { return node_partition.first; },
      [](const EdgeType& edge, const std::pair<uint32_t, uint32_t>& node_partition) {
          return std::make_pair(node_partition.second, edge);
      }, thrill::hash())
  // Local Moving
    .template GroupByKey<std::vector<std::pair<uint32_t, uint32_t>>>(
      [](const std::pair<uint32_t, EdgeType> & edge_with_partition) { return edge_with_partition.first; },
      [&node_count, &total_weight](auto & iterator, const uint32_t & partition) {
        std::set<uint32_t> node_ids_in_partition;

        std::vector<std::map<uint32_t, uint32_t>> adjacency_lists(node_count);

        uint32_t partition_edge_count_upper_bound = 0;
        while (iterator.HasNext()) {
          const EdgeType& edge = iterator.Next().second;
          node_ids_in_partition.insert(edge.tail);
          if (edge.tail == edge.head) {
            adjacency_lists[edge.tail][edge.head] = edge.getWeight() * 2;
          } else {
            adjacency_lists[edge.tail][edge.head] = edge.getWeight();
            adjacency_lists[edge.head][edge.tail] = edge.getWeight();
          }
          partition_edge_count_upper_bound += 2;
        }

        Graph graph(node_count, partition_edge_count_upper_bound, std::is_same<EdgeType, WeightedEdge>::value);
        graph.setEdgesByAdjacencyMatrix(adjacency_lists);
        graph.overrideTotalWeight(total_weight);
        ClusterStore clusters(0, node_count);

        assert(node_ids_in_partition.size() == node_count);
        std::vector<uint32_t> node_ids_in_partition_vector(node_ids_in_partition.begin(), node_ids_in_partition.end());
        Modularity::localMoving(graph, clusters, node_ids_in_partition_vector);

        std::vector<std::pair<uint32_t, uint32_t>> mapping;
        for (uint32_t node : node_ids_in_partition_vector) {
          mapping.push_back(std::make_pair(node, clusters[node] + node_count * partition));
        }
        return mapping;
      })
    .template FlatMap<NodeInfo>(
      [](std::vector<std::pair<uint32_t, uint32_t>> mapping, auto emit) {
        for (const std::pair<uint32_t, uint32_t>& pair : mapping) {
          emit(NodeInfo { pair.first, pair.second });
        }
      });

  auto cluster_count = bloated_node_clusters
    .Map([](const NodeInfo & node_cluster) { return node_cluster.data; })
    .ReduceByKey(
      [](const uint32_t & cluster_id) { return cluster_id; },
      [](const uint32_t & id, const uint32_t &) {
        return id;
      })
    .Size();

  if (node_count == cluster_count) {
    return bloated_node_clusters;
  }

  // Build Meta Graph
  auto meta_graph_edges = edge_list
    .InnerJoinWith(bloated_node_clusters,
      [](const EdgeType& edge) { return edge.tail; },
      [](const NodeInfo& node_cluster) { return node_cluster.id; },
      [](EdgeType edge, const NodeInfo& node_cluster) {
          edge.tail = node_cluster.data;
          return edge;
      }, thrill::hash())
    .InnerJoinWith(bloated_node_clusters,
      [](const EdgeType& edge) { return edge.head; },
      [](const NodeInfo& node_cluster) { return node_cluster.id; },
      [](EdgeType edge, const NodeInfo& node_cluster) {
          edge.head = node_cluster.data;
          return edge;
      }, thrill::hash())
    .Map([](EdgeType edge) { return WeightedEdge { edge.tail, edge.head, edge.getWeight() }; })
    .ReduceByKey(
      [&node_count](WeightedEdge edge) { return node_count * edge.tail + edge.head; },
      [](WeightedEdge edge1, WeightedEdge edge2) { return WeightedEdge { edge1.tail, edge1.head, edge1.weight + edge2.weight }; })
    // turn loops into forward and backward arc
    .template FlatMap<WeightedEdge>(
      [](const WeightedEdge & edge, auto emit) {
        if (edge.tail == edge.head) {
          emit(WeightedEdge { edge.tail, edge.head, edge.weight / 2 });
          emit(WeightedEdge { edge.tail, edge.head, edge.weight / 2 });
        } else {
          emit(edge);
        }
      })
    .Sort(
      [](const WeightedEdge & e1, const WeightedEdge & e2) {
        return (e1.tail == e2.tail && e1.head < e2.head) || (e1.tail < e2.tail);
      })
    .Collapse();

  // Recursion on meta graph
  auto meta_clustering = louvain(meta_graph_edges);

  return bloated_node_clusters
    .InnerJoinWith(meta_clustering,
      [](const NodeInfo& node_cluster) { return node_cluster.data; },
      [](const NodeInfo& meta_node_cluster) { return meta_node_cluster.id; },
      [](const NodeInfo& node_cluster, const NodeInfo& meta_node_cluster) {
          return NodeInfo { node_cluster.id, meta_node_cluster.data };
      }, thrill::hash());
}

int main(int, char const *argv[]) {
  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
  Modularity::rng = std::default_random_engine(seed);

  return thrill::Run([&](thrill::Context& context) {
    auto edges = thrill::ReadLines(context, argv[1])
      .Filter([](const std::string& line) { return !line.empty() && line[0] != '#'; })
      .template FlatMap<Edge>(
        [](const std::string& line, auto emit) {
          std::istringstream line_stream(line);
          uint32_t tail, head;

          if (line_stream >> tail >> head) {
            tail--;
            head--;
            emit(Edge { tail, head });
            emit(Edge { head, tail });
          } else {
            die(std::string("malformatted edge: ") + line);
          }
        })
      .Sort([](const Edge & e1, const Edge & e2) {
        return (e1.tail == e2.tail && e1.head < e2.head) || (e1.tail < e2.tail);
      });

    louvain(edges).Print("Clusters");
  });
}

