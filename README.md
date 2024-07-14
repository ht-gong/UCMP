# UCMP-sim
Packet-level simulation code for SIGCOMM 2024 paper (UCMP) "Uniform-Cost Multi-Path Routing for Reconfigurable Data Center Networks"

## Table of contents
1. [Requirements and reference hardware](#requirements-and-reference-hardware)
2. [Description](#description)
3. [Running the simulations and reproducing the results](#running-the-simulations-and-reproducing-the-results)
4. [Generating topologies and flow files](#generating-topologies-and-flow-files)

## Requirements and reference hardware

### Building
Suggested compiler version from original paper is `g++-7`.
We compile using `g++ 10.2.1` on Debian 11.

We noticed that compilation often fails with several `clang` versions, we suggest sticking with `gcc`.

### Reference hardware
We ran all of our simulations on a Debian 11 machine with a Intel Xeon Gold 5317 CPU and 128GB ram.
The simulator runs on a single core.
We give a loose bound of required time and resources for each simulation.

## Description

The /src directory contains the packet simulator source code. There is a separate simulator for each network type (i.e., UCMP, Opera). The packet simulator is an extension of the htsim Opera simulator (https://github.com/TritonNetworking/opera-sim).

### Repo Structure:
```
/
├─ topologies/ -- network topology files
├─ src/ -- source for the htsim simulator
│  ├─ optiroute/ -- for UCMP, ksp
│  ├─ opera/ -- for Opera(NSDI '20), RotorNet(SIGCOMM '17)
├─ run/ -- where simulator runs are initiated, results and plotting scripts are stored
├─ traffic --  for generating synthetic traffic traces
```

## Build instructions:

From the main directory, you should compile all the executables using:

#### Opera

```
cd src/opera
make
cd datacenter
make
```

#### UCMP

```
cd src/optiroute
make
cd datacenter
make
```

Avoid using `make -j` when compiling the `libhtsim.a` library as it may interfere with the Makefile configuration.
The executables will be built in the `datacenter/` subdirectories.

## Running the simulations and reproducing the results

### Running the simulations via automated script

The most straight-forward way to reproduce a particular figure is to run the `runs.sh` script in the corresponding figure directory.
By default, the script will run all the required simulator runs sequentially for 0.8s **simulation** time, which is the duration we ran for the paper.
Furthermore, the script will also try finding existing complete simulation outputs before starting a certain simulation.
After all simulations are done running, figures will be plotted in `figures/`.
You can check all available options using `runs.sh -h`.

As simulations are large scale, some can take up to *several days to finish while requiring 100GB+ ram.*
We provide a rough time estimation and resource requirement for each of them on our hardware if running with the default setting (i.e., no parameters provided to `runs.sh`), included in this [section](#estimation-of-running-time-and-memory-consumption).

What we **strongly recommend** instead is to run the simulations for a shorter amount of time.

The `runs.sh` script can take a `-s simtime` parameter to specify a different running time in seconds (e.g., `-s 0.1`), which will greatly speed up evaluation.
For the Websearch trace (`traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim`), setting *simtime* to 0.1/0.2 seconds will produce a very similar curve as shown in the paper.
For the Datamining trace (`traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim`), setting the *simtime* to 0.1/0.2s reveals the trend of short flows. 
However, the FCT of several significantly longer flows (>30MB) will be missing until the default *simtime* of 0.8s is reached.

We also provide raw output files for each of the simulations in a google drive [here](https://drive.google.com/drive/folders/1rUjKdxsWR7vzuIzpBmcU2Z4s-Oeh-6Mi?usp=share_link).

As simulations are fully deterministic, as long as you don't change the input (i.e., use our same flow and topology files) you will get an identical output.
This way you could also choose to `diff` a short simulation output with the first part of a full output and verify they match (e.g., `diff shorter.txt <(head -n $(wc -l < shorter.txt) full.txt)` ), then simply plot the provided full output.


### Running the simulations manually

The executable for a particular network+protocol combination is `src/network/datacenter/main_protocol_dynexpTopology`, where network is either `opera` for Opera or `optiroute` for UCMP, and `protocol` is either `dctcp` or `ndp`, (e.g., `src/opera/datacenter/main_ndp_dynexpTopology` to run Opera+NDP).

#### Parameters

There are numerous parameters to the simulator, some dependent on the network and transport.

##### General parameters

* `-simtime t` run the simulation up to *t* seconds
* `-utiltime t` sample link utilization and queue size every *t* seconds
* `-topfile f` use topology file `f` (note: Opera and UCMP use differently formatted files)
* `-flowfile f` run flows from flow file `f`
* `-q q` set maximum queue size for all queues to `q` MTU-size packets
* `-cutoff c` short/long flows cutoff `c` to switch to VLB routing
* `-cwnd c` initial window `c` for DCTCP/NDP protocols

##### UCMP parameters
* `-slicedur t` set time slice duration to `t`
* `-routing R` set routing algorithm to `R` (options: `OptiRoute`, `KShortest`, `VLB`)
* `-norlb` disable RLB transport (useful to hasten simulations with no flows running VLB routing)

##### DCTCP parameters
* `-dctcpmarking m` set ECN threshold to `m` packets

##### NDP parameters
* `-pullrate p` set rate of pulling to `p` of the maximum link bandwidth

#### Reading the output

##### FCT
Flow completion time of flows is reported as:

`FCT <src> <dst> <flowsize> <fct> <start_time> <reorder_info> <rtx_info> <buffering_info> <max_hops_taken> <last_hops_taken> <total_hops_taken>`

##### Utilization
Global utilization is periodically reported as:

`Util <downlink_util> <uplink_util> <current_simtime>`

Per-link utilization is periodically reported as:

`Pipe <tor> <port> <util>`

#### Checking progress

To check how far a simulation is,  you may want to execute the following bash command in the `/run/FigureX` folders, and substitute ``$SIMULATOR_OUTPUT`` with the output name to examine the simulation progress, when the script is still running: 

``grep FCT $SIMULATOR_OUTPUT | tail | awk '{print $5+$6;}'``

The output would be the time elapsed from the start of the simulation in milliseconds.
Each simulation should finish when this output approaches the target end time.


## Generating topologies and flow files

Generating topologies and flow files involves randomness so we strongly suggest to stick with our provided ones if you want to exactly reproduce the results.
Nevertheless, you may be interested in generating new flow traces or topologies for further experiments.

### Generating flow traces

The input files and scripts to generate new flow traces can be found in `traffic/`.
A MATLAB script such as `generate_websearch_traffic.m` or `generate_datamining_traffic.m` will take topology information (`Nrack`, `Hosts_p_rack`), target load (`loadfrac0`), target trace duration (`totaltime`), link bandwidth (`linkrate`), and finally a flow size distribution (e.g., `websearch.csv`, `datamining.csv`) to generate flows with inter-arrival times based on a Poisson distribution.

### Generating topology files

For each topology, refer to the specific subdirectory in `topologies/`.
Note that we could not always reproduce topologies from the original Opera paper (`topologies/opera`), and just got the original output files from the authors.

### Estimation of Running Time and Memory Consumption
These numbers are grouped by figures, and measured on our server with Intel(R) Xeon(R) Gold 5317 CPU and 128GB memory. Very time/memory-consuming runs are bolded.


Figure 6ac and 7
| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_ndp, SIMTIME 0.8sec    | 10hrs | 3GB  |
| OptiRoute_websearch_dctcp, SIMTIME 0.8sec  | 10hrs | 3GB  |
| Opera_5paths_websearch_ndp, SIMTIME 0.8sec | 15hrs | 3GB  |
| Opera_1path_websearch_ndp, SIMTIME 0.8sec  | 15hrs | 3GB  |
| VLB_websearch_rotorlb, SIMTIME 0.8sec      | 10hrs | 3GB  |
| ksp_topK=1_websearch_dctcp, SIMTIME 0.8sec | 10hrs | 3GB  |
| ksp_topK=5_websearch_dctcp, SIMTIME 0.8sec | 10hrs | 3GB  |

Figure 6bd
| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_datamining_ndp, SIMTIME 0.8sec    | 10hrs | **100GB**  |
| OptiRoute_datamining_dctcp, SIMTIME 0.8sec  | 10hrs | **100GB**   |
| Opera_5paths_datamining_ndp, SIMTIME 0.8sec | 15hrs | **100GB**   |
| Opera_1path_datamining_ndp, SIMTIME 0.8sec  | 15hrs | **100GB**   |
| VLB_datamining_rotorlb, SIMTIME 0.8sec      | 10hrs | **80GB**   |
| ksp_topK=1_datamining_dctcp, SIMTIME 0.8sec | 10hrs | **80GB**   |
| ksp_topK=5_datamining_dctcp, SIMTIME 0.8sec | 10hrs | **80GB**   |

Figure 8

| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_dctcp, SIMTIME 0.8sec    | 10hrs | 3GB  |
| OptiRoute_websearch_dctcp_aging, SIMTIME 0.8sec  | 10hrs | 3GB   |


Figure 9

| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_dctcp_cfg_10ns, SIMTIME 0.8sec    | 10hrs | 3GB  |
| OptiRoute_websearch_dctcp_cfg_1us, SIMTIME 0.8sec  | 10hrs | 3GB   |
| OptiRoute_websearch_dctcp_cfg_10us, SIMTIME 0.8sec  | 10hrs | 3GB   |


Figure 10

| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_dctcp_alpha_0.3, SIMTIME 0.8sec  | 10hrs | 3GB  |
| OptiRoute_websearch_dctcp_alpha_0.5, SIMTIME 0.8sec  | 10hrs | 3GB   |
| OptiRoute_websearch_dctcp_alpha_0.7, SIMTIME 0.8sec  | 10hrs | 3GB   |

Figure 11

| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_dctcp_slice_1us, SIMTIME 0.8sec  | **80hrs** | 3GB  |
| OptiRoute_websearch_dctcp_slice_10us, SIMTIME 0.8sec  | 10hrs | 3GB   |
| OptiRoute_websearch_dctcp_slice_50us, SIMTIME 0.8sec  | 8hrs | 3GB   |
| OptiRoute_websearch_dctcp_slice_300us, SIMTIME 0.8sec  | 6hrs | 3GB   |
