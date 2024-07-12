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
We provide a rough time estimation and resource requirement for each of them on our hardware if running with the default setting (i.e., no parameters provided to `runs.sh`).
What we **strongly recommend** instead is to run the simulations for a shorter amount of time.
The `runs.sh` script can take a `-s simtime` parameter to specify a different running time in seconds (e.g., `-s 0.1`), which will greatly speed up evaluation.
For the Websearch trace (`traffic/websearch_uniform_40percLoad_0.5sec_648hosts.htsim`), setting *simtime* to 0.1/0.2 seconds will produce a very similar curve as shown in the paper.
For the Datamining trace (`traffic/datamining_uniform_40percLoad_0.5sec_648hosts.htsim`), setting the *simtime* to 0.1/0.2s reveals the trend of short flows. 
However, the FCT of several significantly longer flows (>30MB) will be missing until the default *simtime* of 0.8s is reached.

We also provide raw output files for each of the simulations.
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
