#!/usr/bin/env python3

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

import json
import glob
import sys
import re
import multiprocessing

data = {}

for path in glob.glob(sys.argv[1]):
  for typename, items in json.load(open(path)).items():
    if typename in data:
      data[typename].update(items)
    else:
      data[typename] = items

frames = { typename: pd.DataFrame.from_dict(items, orient='index') for typename, items in data.items() }

from os import listdir, path
from subprocess import check_output

file_pattern = re.compile('^(.+-(\d+))-\d+-\d+\.bin$')
jobid_pattern = re.compile('^clusterings/.+-(\d+)-@@@@-#####.bin$')

bin_to_algo = {
  "/home/kit/iti/ji4215/code/release/dlslm": "synchronous local moving with modularity",
  "/home/kit/iti/ji4215/code/release/dlslm_no_contraction": "synchronous local moving with modularity",
  "/home/kit/iti/ji4215/code/release/dlslm_map_eq": "synchronous local moving with map equation"
}

base_dir = path.dirname(sys.argv[1])

def work(clustering_path):
  if not (frames['clustering']['path'] == clustering_path).any():
    jobid = int(jobid_pattern.match(clustering_path).group(1))
    df = frames['algorithm_run'].merge(frames['program_run'].loc[frames['program_run']["job_id"] == jobid], left_on='program_run_id', right_index=True)
    job_data = df.iloc[0]
    index = df.index[0]
    graph_files = job_data['graph'].replace('/home/kit/iti/ji4215', '/algoDaten/zeitz')
    clustering_files = path.join(base_dir, clustering_path.replace('@@@@-#####', '*'))

    print(["./streaming_clustering_analyser", graph_files, clustering_files, job_data['node_count']])
    output = check_output(["./streaming_clustering_analyser", graph_files, clustering_files, str(job_data['node_count'])]).decode("utf-8")

    tmpfile_name = "_tmp_{}".format(jobid)
    tmpfile = open(tmpfile_name, "w")
    tmpfile.write(output)
    tmpfile.write("#LOG# clustering/0/path: {}\n".format(clustering_path))
    tmpfile.write("#LOG# clustering/0/source: computation\n")
    tmpfile.write("#LOG# clustering/0/algorithm_run_id: {}\n".format(index))
    tmpfile.close()
    check_output(["./report_to_json.rb", tmpfile_name])
    check_output(["rm", tmpfile_name])

# clusterings/me-14169048-@@@@-#####.bin
count = multiprocessing.cpu_count()
pool = multiprocessing.Pool(processes=count)
pool.map(work, set(["clusterings/{}-@@@@-#####.bin".format(file_pattern.match(file).group(1)) for file in listdir(path.join(base_dir, "clusterings")) if file_pattern.match(file)]))
