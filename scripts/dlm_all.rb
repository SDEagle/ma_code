#!/usr/bin/env ruby

graphs = {
  'graphs/graph_50_10000_mu_0.5_100000-preprocessed-*.bin' => [:fast, [:s, :m, :xl]],
  'graphs/graph_50_10000_mu_0.5_1000000-preprocessed-*.bin' => [:standard, [:s, :m, :xl]],
  'graphs/graph_50_10000_mu_0.5_10000000-preprocessed-*.bin' => [:standard, [:s, :m, :xl]],
  # 'graphs/graph_50_10000_mu_0.5_100000000-preprocessed-*.bin' => [:slow, [:m, :xl]],
  'graphs/hypercubegraph23-preprocessed-*.bin' => [:slow, [:s, :m, :xl]],
  'graphs/uk-2002.metis-preprocessed-*.bin' => [:standard, [:s, :m, :xl]],
  # 'graphs/uk-2007-05.metis-preprocessed-*.bin' => [:slow, [:s, :m, :xl]],
  'graphs/in-2004.metis-preprocessed-*.bin' => [:standard, [:s, :m, :xl]],
  'graphs/com-friendster-preprocessed-*.bin' => [:standard, [:s, :m, :xl]],
  'graphs/com-lj.ungraph-preprocessed-*.bin' => [:standard, [:s, :m, :xl]],
  'graphs/com-orkut.ungraph-preprocessed-*.bin' => [:standard, [:s, :m, :xl]],
  'graphs/com-youtube.ungraph-preprocessed-*.bin' => [:fast, [:s, :m, :xl]],
  'graphs/com-amazon.ungraph-preprocessed-*.bin' => [:fast, [:s, :m, :xl]]
}

time_configs = {
  fast: '-l walltime=00:10:00',
  standard: '-l walltime=01:00:00',
  slow: '-l walltime=05:00:00'
}

node_configs = {
  xs: '-l nodes=1:ppn=2 -v THRILL_WORKERS_PER_HOST=2 -l pmem=64000mb -q singlenode',
  s: '-l nodes=1:ppn=5 -v THRILL_WORKERS_PER_HOST=5 -l pmem=12000mb -q singlenode',
  ms: '-l nodes=2:ppn=5 -v THRILL_WORKERS_PER_HOST=5 -l pmem=12000mb -q multinode',
  m: '-l nodes=4:ppn=5 -v THRILL_WORKERS_PER_HOST=5 -l pmem=12000mb -q multinode',
  l: '-l nodes=8:ppn=5 -v THRILL_WORKERS_PER_HOST=5 -l pmem=12000mb -q multinode',
  xl: '-l nodes=16:ppn=5 -v THRILL_WORKERS_PER_HOST=5 -l pmem=12000mb -q multinode',
}


graphs.each do |graph, configs|
  configs[1].each do |node_config|
    command_result = `msub -v GRAPH=#{graph[0]} -v CLUSTERING=clusterings/dlm #{time_configs[configs[0]]} #{node_configs[node_config]} ~/code/scripts/moab/no_default_dlm.sh`
    puts "#{graph[0]} #{configs[1]} #{node_config} #{command_result}"
  end
end
