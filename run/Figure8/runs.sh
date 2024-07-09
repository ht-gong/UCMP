#!/bin/bash

../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -utiltime 5e-04 -simtime 0.801 -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing OptiRoute -topfile ../../topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > OptiRoute_websearch_dctcp_0.5sec_0.8sec_50us.txt

../../src/optiroute/datacenter/htsim_dctcp_dynexpTopology -aging -utiltime 5e-04 -simtime 0.801 -cutoff 150000000000 -rlbflow 0 -cwnd 20 -q 300 -routing OptiRoute -topfile ../../topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt -flowfile ../../traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim -dctcpmarking 32 -slicedur 50000000 > OptiRoute_websearch_dctcp_0.5sec_0.8sec_50us_aging.txt

python3 set_config.py
python3 FCT.py