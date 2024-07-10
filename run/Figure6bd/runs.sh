#!/bin/bash

# Be careful that the following runs are extremely memory-intensive; each run takes up 50 - 100 GBs in RAM.

../../src/optiroute/datacenter/htsim_ndp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 35000000 -rlbflow 0 -cwnd 20 -q 65 -pullrate 1 -alphazero -routing OptiRoute -topfile ../../topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim -slicedur 50000000 > OptiRoute_datamining_ndp_0.5sec_0.8sec_50us.txt

../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 35000000 -rlbflow 0 -cwnd 20 -q 300 -alphazero -routing OptiRoute -topfile ../../topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > OptiRoute_datamining_dctcp_0.5sec_0.8sec_50us.txt

../../src/opera/datacenter/htsim_ndp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 15000000 -cwnd 20 -q 65 -pullrate 1 -topfile ../../topologies/dynexp_50us_10nsrc_5paths.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim > Opera_5paths_datamining_ndp_0.5sec_0.8sec_50us.txt

../../src/opera/datacenter/htsim_ndp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 15000000 -cwnd 20 -q 65 -pullrate 1 -topfile ../../topologies/dynexp_50us_10nsrc_1path.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim > Opera_1path_datamining_ndp_0.5sec_0.8sec_50us.txt

../../src/opera/datacenter/htsim_ndp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 0 -cwnd 20 -q 65 -pullrate 1 -topfile ../../topologies/dynexp_50us_10nsrc_1path.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim > VLB_datamining_rotorlb_opera_0.5sec_0.8sec_50us.txt

../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing KShortest -topfile ../../topologies/general_from_dynexp_N=108_topK=1.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > ksp_topK=1_datamining_dctcp_0.5sec_0.8sec_50us.txt

../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing KShortest -topfile ../../topologies/general_from_dynexp_N=108_topK=5.txt -flowfile ../../traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > ksp_topK=5_datamining_dctcp_0.5sec_0.8sec_50us.txt


python3 set_config.py

python3 FCT.py

python3 BE_CDF.py
python3 BE_BAR.py