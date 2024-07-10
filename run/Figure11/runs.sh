#!/bin/bash

# Be careful that this one run, due to its 1us small time-slice, is extremely time consuming; it takes up to 60 hrs to complete.
../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing OptiRoute -topfile ../../topologies/slice1us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 1000000 > OptiRoute_websearch_dctcp_0.5sec_0.8sec_1us.txt

../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing OptiRoute -topfile ../../topologies/slice10us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 10000000 > OptiRoute_websearch_dctcp_0.5sec_0.8sec_10us.txt
../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing OptiRoute -topfile ../../topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > OptiRoute_websearch_dctcp_0.5sec_0.8sec_50us.txt
../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing OptiRoute -topfile ../../topologies/slice300us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 300000000 > OptiRoute_websearch_dctcp_0.5sec_0.8sec_300us.txt

python3 set_config.py

python3 FCT.py

python3 BE_CDF.py
python3 BE_BAR.py