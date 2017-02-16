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

#include <ostream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <set>
#include <map>

#include <graph.hpp>
#include <modularity.hpp>
#include <cluster_store.hpp>

#include "thrill_graph.hpp"
#include "input.hpp"

using ClusterId = NodeId;

struct Node {
  NodeId id;
  Weight degree;
};

struct NodeInfo {
  NodeId id;
  ClusterId data;
};

std::ostream& operator << (std::ostream& os, NodeInfo& node_info) {
  return os << node_info.id << ": " << node_info.data;
}

template<class EdgeType>
thrill::DIA<NodeInfo> louvain(thrill::DIA<EdgeType>& edge_list) {
  edge_list = edge_list.Cache();

  auto nodes = edge_list
    .Keep()
    .Map([](const EdgeType & edge) { return Node { edge.tail, 1 }; })
    .ReduceByKey(
      [](const Node & node) { return node.id; },
      [](const Node & node1, const Node & node2) { return Node { node1.id, node1.degree + node2.degree }; })
    .Sort([](const Node & node1, const Node & node2) { return node1.id < node2.id; });

  const uint64_t node_count = nodes.Keep().Size();
  const uint64_t total_weight = edge_list.Keep().Map([](const EdgeType & edge) { return edge.getWeight(); }).Sum() / 2;

  auto edge_partitions = nodes
    .Keep()
    // Map to degree times partition
    .template FlatMap<uint32_t>(
      [](const Node & node, auto emit) {
        for (uint32_t i = 0; i < node.degree; i++) {
          emit(0); // dummy partition
        }
      });

  // Local Moving
  auto bloated_node_clusters = edge_list
    .Keep()
    .Zip(edge_partitions, [](const EdgeType & edge, const uint32_t & partition) { return std::make_pair(partition, edge); })
    .template GroupByKey<std::vector<std::pair<uint32_t, uint32_t>>>(
      [](const std::pair<uint32_t, EdgeType> & edge_with_partition) { return edge_with_partition.first; },
      [&node_count, &total_weight](auto & iterator, const uint32_t & partition) {
        std::set<uint32_t> node_ids_in_partition;

        std::vector<std::map<uint32_t, uint32_t>> adjacency_lists(node_count);

        uint32_t partition_edge_count_upper_bound = 0;
        while (iterator.HasNext()) {
          const EdgeType& edge = iterator.Next().second;
          assert(edge.head < node_count);
          assert(edge.tail < node_count);
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
        ClusterStore clusters(node_count);

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
      })
    .Sort(
      [](const NodeInfo & node_cluster1, const NodeInfo & node_cluster2) {
        return node_cluster1.data < node_cluster2.data;
      })
    .Cache();

  auto cluster_sizes = bloated_node_clusters
    .Keep()
    .Map([](const NodeInfo & node_cluster) { return std::make_pair(node_cluster.data, 1u); })
    .ReduceByKey(
      [](const std::pair<uint32_t, uint32_t> & cluster_size) { return cluster_size.first; },
      [](const std::pair<uint32_t, uint32_t> & cluster_size1, const std::pair<uint32_t, uint32_t> & cluster_size2) {
        return std::make_pair(cluster_size1.first, cluster_size1.second + cluster_size2.second);
      })
    .Sort([](const std::pair<uint32_t, uint32_t> & cluster_size1, const std::pair<uint32_t, uint32_t> & cluster_size2) { return cluster_size1.first < cluster_size2.first; })
    // cleanup ids
    .ZipWithIndex([](const std::pair<uint32_t, uint32_t> & cluster_size, const uint32_t& index) { return std::make_pair(index, cluster_size.second); });

  auto node_clusters = cluster_sizes
    .template FlatMap<uint32_t>(
      [](const std::pair<uint32_t, uint32_t> & cluster_size, auto emit) {
        for (uint32_t i = 0; i < cluster_size.second; i++) {
          emit(cluster_size.first);
        }
      })
    .Zip(bloated_node_clusters, [](const uint32_t new_id, const NodeInfo & node_cluster) { return NodeInfo { node_cluster.id, new_id }; })
    .Cache();

  if (nodes.Keep().Size() == cluster_sizes.Keep().Size()) {
    return node_clusters;
  }

  auto clusters = node_clusters
    .Keep()
    .Sort(
      [](const NodeInfo & node_cluster1, const NodeInfo & node_cluster2) {
        return node_cluster1.id < node_cluster2.id;
      })
    .Map([](const NodeInfo & node_cluster) { return node_cluster.data; });

  auto cluster_id_times_degree = nodes
    .Zip(clusters, [](const Node & node, const uint32_t cluster_id) { return std::make_pair(node.degree, cluster_id); })
    .template FlatMap<uint32_t>(
      [](std::pair<size_t, uint32_t> degree_and_cluster, auto emit) {
        for (uint32_t i = 0; i < degree_and_cluster.first; i++) {
          emit(degree_and_cluster.second);
        }
      });

  // Build Meta Graph
  auto meta_graph_edges = edge_list
    // Translate Ids
    .Zip(cluster_id_times_degree,
      [](EdgeType edge, uint32_t new_id) {
        edge.tail = new_id;
        return edge;
      })
    .Map(
      [](EdgeType edge) {
        uint32_t tmp = edge.head;
        edge.head = edge.tail;
        edge.tail = tmp;
        return edge;
      })
    .Sort(
      [](const EdgeType & e1, const EdgeType & e2) {
        return (e1.tail == e2.tail && e1.head < e2.head) || (e1.tail < e2.tail);
      })
    .Zip(cluster_id_times_degree,
      [](EdgeType edge, uint32_t new_id) {
        edge.tail = new_id;
        return edge;
      })
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

  // translate meta clusters and return
  auto new_cluster_ids_times_size = meta_clustering
    .Zip(cluster_sizes,
      [](const NodeInfo & meta_node_cluster, const std::pair<uint32_t, uint32_t> & cluster_size) {
        assert(meta_node_cluster.id == cluster_size.first);
        return std::make_pair(meta_node_cluster.data, cluster_size.second);
      })
    .template FlatMap<uint32_t>(
      [](const std::pair<uint32_t, uint32_t> & cluster_size, auto emit) {
        for (uint32_t i = 0; i < cluster_size.second; i++) {
          emit(cluster_size.first);
        }
      });

  return node_clusters
    .Sort(
      [](const NodeInfo & node_cluster1, const NodeInfo & node_cluster2) {
        return node_cluster1.data < node_cluster2.data;
      })
    .Zip(new_cluster_ids_times_size,
      [](const NodeInfo & node_cluster, uint32_t new_cluster_id) {
        return NodeInfo { node_cluster.id, new_cluster_id };
      })
    .Sort(
      [](const NodeInfo & node_cluster1, const NodeInfo & node_cluster2) {
        return node_cluster1.id < node_cluster2.id;
      });
}

int main(int, char const *argv[]) {
  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
  Modularity::rng = std::default_random_engine(seed);

  return thrill::Run([&](thrill::Context& context) {
    context.enable_consume();

    auto graph = Input::readGraph(argv[1], context);

    auto edges = graph.edge_list
      .Sort([](const Edge & e1, const Edge & e2) {
        return (e1.tail == e2.tail && e1.head < e2.head) || (e1.tail < e2.tail);
      });

    size_t cluster_count = louvain(edges)
      .Map([](const NodeInfo& node_cluster) { return node_cluster.data; })
      .ReduceByKey(
        [](const uint32_t cluster) { return cluster; },
        [](const uint32_t cluster, const uint32_t) {
          return cluster;
        })
      .Size();
    std::cout << "Result: " << cluster_count << std::endl;
  });
}

