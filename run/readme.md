## Compiling the executables

From the main directory, you should compile all the executables:

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

## Running the simulations via automated script

The most straight-forward way to reproduce a particular figure is to run the `runs.sh` script in the corresponding figure directory.
By default, the script will run all the required simulator runs sequentially for 0.8s **simulation** time, which is the duration we ran for the paper.
Furthermore, the script will also try finding existing complete simulation outputs before starting a certain simulation.
After all simulations are done running, figures will be plotted in `figures/`.
You can check all available options using `runs.sh -h`.

As simulations are large scale, some can take up to several days to finish while requiring 100GB+ ram.
We provide a rough time estimation and resource requirement for each of them on our hardware if running with the default setting (i.e., no parameters provided to `runs.sh`). These are included in this [section](###estimation-of-running-time-and-memory-consumption).


What we **strongly recommend** instead is to run the simulations for a shorter amount of time.
The `runs.sh` script can take a `-s simtime` parameter to specify a different running time in seconds (e.g., `-s 0.1`), which will greatly speed up evaluation.
For the Websearch trace (`traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim`), setting *simtime* to 0.1/0.2 seconds will produce a very similar curve as shown in the paper.
For the Datamining trace (`traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim`), setting the *simtime* to 0.1/0.2s reveals the trend of short flows. 
However, the FCT of several significantly longer flows (>30MB) will be missing until the default *simtime* of 0.8s is reached.

We also provide raw output files for each of the simulations in a google drive [here](https://drive.google.com/drive/folders/1rUjKdxsWR7vzuIzpBmcU2Z4s-Oeh-6Mi?usp=share_link).

As simulations are fully deterministic, as long as you don't change the input (i.e., use our same flow and topology files) you will get an identical output.
This way you could also choose to `diff` a short simulation output with the first part of a full output and verify they match (e.g., `diff shorter.txt <(head -n $(wc -l < shorter.txt) full.txt)` ), then simply plot the provided full output.


## Running the simulations manually

The executable for a particular network+protocol combination is `src/network/datacenter/main_protocol_dynexpTopology`, where network is either `opera` for Opera or `optiroute` for UCMP, and `protocol` is either `dctcp` or `ndp`, (e.g., `src/opera/datacenter/main_ndp_dynexpTopology` to run Opera+NDP).

### Parameters

There are numerous parameters to the simulator, some dependent on the network and transport.

#### General parameters

* `-simtime t` run the simulation up to *t* seconds
* `-utiltime t` sample link utilization and queue size every *t* seconds
* `-topfile f` use topology file `f` (note: Opera and UCMP use differently formatted files)
* `-flowfile f` run flows from flow file `f`
* `-q q` set maximum queue size for all queues to `q` MTU-size packets
* `-cutoff c` short/long flows cutoff `c` to switch to VLB routing
* `-cwnd c` initial window `c` for DCTCP/NDP protocols

#### UCMP parameters
* `-slicedur t` set time slice duration to `t`
* `-routing R` set routing algorithm to `R` (options: `OptiRoute`, `KShortest`, `VLB`)
* `-norlb` disable RLB transport (useful to hasten simulations with no flows running VLB routing)

#### DCTCP parameters
* `-dctcpmarking m` set ECN threshold to `m` packets

#### NDP parameters
* `-pullrate p` set rate of pulling to `p` of the maximum link bandwidth

### Reading the output

#### FCT
Flow completion time of flows is reported as:

`FCT <src> <dst> <flowsize> <fct> <start_time> <reorder_info> <rtx_info> <buffering_info> <max_hops_taken> <last_hops_taken> <total_hops_taken>`

#### Utilization
Global utilization is periodically reported as:

`Util <downlink_util> <uplink_util> <current_simtime>`

Per-link utilization is periodically reported as:

`Pipe <tor> <port> <util>`

### Checking progress

To check how far a simulation is,  you may want to execute the following bash command in the `/run/FigureX` folders, and substitute ``$SIMULATOR_OUTPUT`` with the output name to examine the simulation progress, when the script is still running: 
    ``grep FCT $SIMULATOR_OUTPUT | tail | awk '{print $5+$6;}'``. 


### Estimation of Running Time and Memory Consumption
These numbers are grouped by figures, and measured on our server with Intel(R) Xeon(R) Gold 5317 CPU and 128GB memory. Very time/memory-consuming runs are bolded.


Figure 6ac and 7
| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_ndp, Sim time 0.5sec    | 10hrs | 3GB  |
| OptiRoute_websearch_dctcp, Sim time 0.5sec  | 10hrs | 3GB  |
| Opera_5paths_websearch_ndp, Sim time 0.5sec | 15hrs | 3GB  |
| Opera_1path_websearch_ndp, Sim time 0.5sec  | 15hrs | 3GB  |
| VLB_websearch_rotorlb, Sim time 0.5sec      | 10hrs | 3GB  |
| ksp_topK=1_websearch_dctcp, Sim time 0.5sec | 10hrs | 3GB  |
| ksp_topK=5_websearch_dctcp, Sim time 0.5sec | 10hrs | 3GB  |

Figure 6bd
| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_datamining_ndp, Sim time 0.5sec    | 10hrs | **100GB**  |
| OptiRoute_datamining_dctcp, Sim time 0.5sec  | 10hrs | **100GB**   |
| Opera_5paths_datamining_ndp, Sim time 0.5sec | 15hrs | **100GB**   |
| Opera_1path_datamining_ndp, Sim time 0.5sec  | 15hrs | **100GB**   |
| VLB_datamining_rotorlb, Sim time 0.5sec      | 10hrs | **80GB**   |
| ksp_topK=1_datamining_dctcp, Sim time 0.5sec | 10hrs | **80GB**   |
| ksp_topK=5_datamining_dctcp, Sim time 0.5sec | 10hrs | **80GB**   |

Figure 8

| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_dctcp, Sim time 0.5sec    | 10hrs | 3GB  |
| OptiRoute_websearch_dctcp_aging, Sim time 0.5sec  | 10hrs | 3GB   |


Figure 9

| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_dctcp_cfg_10ns, Sim time 0.5sec    | 10hrs | 3GB  |
| OptiRoute_websearch_dctcp_cfg_1us, Sim time 0.5sec  | 10hrs | 3GB   |
| OptiRoute_websearch_dctcp_cfg_10us, Sim time 0.5sec  | 10hrs | 3GB   |


Figure 10

| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_dctcp_alpha_0.3, Sim time 0.5sec  | 10hrs | 3GB  |
| OptiRoute_websearch_dctcp_alpha_0.5, Sim time 0.5sec  | 10hrs | 3GB   |
| OptiRoute_websearch_dctcp_alpha_0.7, Sim time 0.5sec  | 10hrs | 3GB   |

Figure 11

| Run Name                                    | Time  | RAM  |
|---------------------------------------------|-------|------|
| OptiRoute_websearch_dctcp_slice_1us, Sim time 0.5sec  | **80hrs** | 3GB  |
| OptiRoute_websearch_dctcp_slice_10us, Sim time 0.5sec  | 10hrs | 3GB   |
| OptiRoute_websearch_dctcp_slice_50us, Sim time 0.5sec  | 8hrs | 3GB   |
| OptiRoute_websearch_dctcp_slice_300us, Sim time 0.5sec  | 6hrs | 3GB   |

