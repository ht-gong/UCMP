#!/bin/bash

#get relative path
PARENT_PATH=$( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )
cd "$PARENT_PATH"

SIMTIME=${1:-0.8} #default simtime 0.8s
SIMTIME_FULL=${SIMTIME}01 #always add slighly more
SIMTIME_MS=$(echo $SIMTIME*1000 | bc) #to check for simtime inside output file

SUCCESS=true
FILES=()

check_res() {
    if ! grep Util $FNAME | grep -q $SIMTIME_MS; then
        echo "Failed to run $FNAME, file is incomplete!"
        SUCCESS=false
    else
        echo "Successfully finished running $FNAME!"
    fi
}

run_sim () {
echo "Checking for ${FNAME}..."
if ! [[ -e $FNAME ]] || ! grep Util $FNAME | grep -q $SIMTIME_MS; then
    echo "Running ${FNAME}..."
    eval $COMMAND
    check_res
else
    echo "Found complete ${FNAME}, skipping."
fi
}

echo "Running simulations for ${SIMTIME_FULL}s simtime"
printf "\n"

FNAME=OptiRoute_datamining_ndp_0.5sec_${SIMTIME}sec_50us.txt
FILES+=($FNAME)
COMMAND="../../src/optiroute/datacenter/htsim_ndp_dynexpTopology -utiltime 5e-04 -simtime $SIMTIME_FULL -cutoff 35000000 -rlbflow 0 -cwnd 20 -q 65 -pullrate 1 -alphazero -routing OptiRoute -topfile ../../topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim -slicedur 50000000 > $FNAME"
run_sim
printf "\n"

FNAME=OptiRoute_datamining_dctcp_0.5sec_${SIMTIME}sec_50us.txt
FILES+=($FNAME)
COMMAND="../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime $SIMTIME_FULL -cutoff 35000000 -rlbflow 0 -cwnd 20 -q 300 -alphazero -routing OptiRoute -topfile ../../topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > $FNAME"
run_sim
printf "\n"

FNAME=Opera_5paths_datamining_ndp_0.5sec_${SIMTIME}sec_50us.txt
FILES+=($FNAME)
COMMAND="../../src/opera/datacenter/htsim_ndp_dynexpTopology -utiltime 5e-04 -simtime $SIMTIME_FULL -cutoff 15000000 -cwnd 20 -q 65 -pullrate 1 -topfile ../../topologies/dynexp_50us_10nsrc_5paths.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim > $FNAME"
run_sim
printf "\n"

FNAME=Opera_1path_datamining_ndp_0.5sec_${SIMTIME}sec_50us.txt
FILES+=($FNAME)
COMMAND="../../src/opera/datacenter/htsim_ndp_dynexpTopology -utiltime 5e-04 -simtime $SIMTIME_FULL -cutoff 15000000 -cwnd 20 -q 65 -pullrate 1 -topfile ../../topologies/dynexp_50us_10nsrc_1path.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim > $FNAME"
run_sim
printf "\n"

FNAME=VLB_datamining_rotorlb_opera_0.5sec_${SIMTIME}sec_50us.txt
FILES+=($FNAME)
COMMAND="../../src/opera/datacenter/htsim_ndp_dynexpTopology -utiltime 5e-04 -simtime $SIMTIME_FULL -cutoff 0 -cwnd 20 -q 65 -pullrate 1 -topfile ../../topologies/dynexp_50us_10nsrc_1path.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim > $FNAME"
run_sim
printf "\n"

FNAME=ksp_topK=1_datamining_dctcp_0.5sec_${SIMTIME}sec_50us.txt
FILES+=($FNAME)
COMMAND="../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime $SIMTIME_FULL -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing KShortest -topfile ../../topologies/general_from_dynexp_N=108_topK=1.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > $FNAME"
run_sim
printf "\n"

FNAME=ksp_topK=5_datamining_dctcp_0.5sec_${SIMTIME}sec_50us.txt
FILES+=($FNAME)
COMMAND="../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime $SIMTIME_FULL -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing KShortest -topfile ../../topologies/general_from_dynexp_N=108_topK=5.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > $FNAME" 
run_sim
printf "\n"

echo "Finished running simulations, plotting figures..."
if [[ $SUCCESS = false ]]; then
    echo "Warning: some simulations seem to have failed!!! Figures may not plot successfully!"
fi

python3 set_config.py $SIMTIME_MS ${FILES[@]}
python3 FCT.py
python3 BE_CDF.py
python3 BE_BAR.py

echo "Done!"
