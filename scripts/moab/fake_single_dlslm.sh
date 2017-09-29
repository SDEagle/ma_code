#!/bin/bash
#MSUB -N synchronous modularity
#MSUB -l pmem=4570mb
#MSUB -l nodes=2:ppn=28
#MSUB -q multinode
#MSUB -v THRILL_WORKERS_PER_HOST=16
#MSUB -v THRILL_LOCAL=1
#MSUB -v THRILL_RAM=120GiB

module load compiler/gnu/7.1

echo "${MOAB_JOBNAME} running on ${MOAB_PROCCOUNT} cores with ${MOAB_NODECOUNT} tasks and ${THRILL_WORKERS_PER_HOST} threads"

result_id=$(ruby -e "require 'securerandom'; puts SecureRandom.uuid")
executable="$HOME/code/release/dlslm"
echo $executable "$GRAPH" "$CLUSTERING-$MOAB_JOBID-@@@@-#####.bin,$result_id"
exec srun -v \
     --exclusive \
     --ntasks="1" \
     --ntasks-per-node="1" \
     --kill-on-bad-exit \
     --mem=0 \
     $executable "$GRAPH" "$CLUSTERING-$MOAB_JOBID-@@@@-#####.bin,$result_id"