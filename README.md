# Ripple-sim
Packet-level simulation code for SIGCOMM 2024 paper (UCMP) "Uniform-Cost Multi-Path Routing for Reconfigurable Data Center Networks"

## Requirements:

- C++ files: g++-7 compiler

## Description:

- The /src directory contains the packet simulator source code. There is a separate simulator for each network type (e.g. UCMP, Opera). The packet simulator is an extension of the htsim NDP simulator (https://github.com/nets-cs-pub-ro/NDP/tree/master/sim)

### Repo Structure:
```
/
├─ topologies/ -- network topology files
├─ src/ -- source for the htsim simulator
│  ├─ optiroute/ -- for UCMP, ksp
│  ├─ opera/ -- for Opera(NSDI '20), VLB(SIGCOMM '17)
├─ run/ -- where simulator runs are initiated, results and plotting scripts are stored
├─ traffic --  for generating synthetic traffic traces
```

## Build instructions:

- To build both versions (optiroute, opera) of the htsim simulator:
  ```
  cd /src/optiroute
  make
  cd /datacenter
  make

  cd /src/opera
  make
  cd /datacenter
  make
  ```
- The executable will be found in the /datacenter directory and named htsim_...

## Re-creating paper figures:

1. Build both ``optiroute`` and ``opera``.
2. Each sub-directory under ``/run`` corresponds to a figure in the UCMP paper, to obtain results and plot the figures, cd into ``/run/FigureX`` and run the bash script named ``runs.sh``.
  - The network topology file for Opera k=5 (5 candidate paths for each src-dst pair) is too large for the Git repo. Please download it from https://drive.google.com/file/d/1B7F3yTlNVO7C7kCwY9ym055iDyM8XVNq/view and place it within the ``/topologies``.
  - *The simulations can take a long time to finish -- the longest simulation takes ~40 hours on our machine*.
  - *It may be more time-efficient to run each command in parallel, but becareful about the memory consumption -- some runs took up to 100GBs of memory in our server.*

## Typical workflow:

- Compile the simulator as described above.
- Generate/use our pre-existing topology file related to that network type
	- UCMP -- /topologies/slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt 
	- Opera -- /topologies/dynexp_50us_10nsrc_1path.txt 
	- VLB -- /topologies/dynexp_50us_10nsrc_1path.txt
	- K Shortest Paths (KSP) -- /topologies/general_from_dynexp_N=108_topK=1.txt
  If you want to generate them manually, go to the corresponding folder and run the script.
- Generate/use our pre-generated files specifying the traffic 
	- Our pre-generated traffic files are in /traffic
	- If you want to generate them manually you should run the corresponding script.  (e.g. /traffic/generate_traffic.m). The file format is (where src_host and dst_host are indexed from zero)
  ```
  <src_host> <dst_host> <flow_size_bytes> <flow_start_time_nanosec> /newline
  ```
  - If you want to generate them manually, run /traffic/generate_websearch_traffic.m, then run write_to_htsim_file.m
- Specify the simulation parameters and run (e.g. run /Figure8/run.sh).
- Plot the post-processed data (e.g. run /Figure8/set_config.py and then run FCT.py)
