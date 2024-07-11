#!/bin/bash

#get relative path
PARENT_PATH=$( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )
cd "$PARENT_PATH"
source ../run_util.sh

SUCCESS=true
FILES=()

echo "Running simulations for ${SIMTIME_FULL}s simtime"
printf "\n"

FNAME=OptiRoute_websearch_dctcp_0.5sec_${SIMTIME}sec_50us.txt
FILES+=($FNAME)
COMMAND="../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime $SIMTIME_FULL -cutoff 150000000000 -norlb -rlbflow 0 -cwnd 20 -q 300 -routing OptiRoute -topfile ../../topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > $FNAME"
run_sim
printf "\n"

FNAME=OptiRoute_websearch_dctcp_0.5sec_${SIMTIME}sec_50us_aging.txt
FILES+=($FNAME)
COMMAND="../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -aging -utiltime 5e-04 -simtime $SIMTIME_FULL -cutoff 150000000000 -norlb -rlbflow 0 -cwnd 20 -q 300 -routing OptiRoute -topfile ../../topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > $FNAME"
run_sim
printf "\n"

echo "Finished running simulations, plotting figures..."
if [[ $SUCCESS = false ]]; then
    echo "Warning: some simulations seem to have failed!!! Figures may not plot successfully!"
fi

python3 set_config.py $SIMTIME_MS ${FILES[@]}
python3 FCT.py

echo "Done!"
