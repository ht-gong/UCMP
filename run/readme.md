1. Each runs.sh contains several simulator runs. Each would generate an output ``.txt file`` with each line starting with ``<src ToR> <dst ToR> <Flow Size> <Flow Completion Time> <Flow Start Time> ...``, where extra network statistics follows.
    - You may want to execute the following bash command in the `/run/FigureX` folders, and substitute ``$SIMULATOR_OUTPUT`` with the output name to examine the simulation progress, when the script is still running. 
    ``tail -100000 $SIMULATOR_OUTPUT.txt | grep FCT |  awk '{print $5+$6;}'``. 
    The output would be the time elapsed from the start of the simulation in miliseconds. Each simulation should finish when this output approaches 800ms.

2. Each simulation takes from 5~6 hours to 2 days to complete, depending on the configuration and workload. One may want to execute them in parallel to save time, but be careful to monitor the memory usage, especially for the *datamining* simulations -- we observed that memory consumption grew up to 100GBs for a few runs.
    - We have marked these runs with comments in the runs.sh files.

3. All settings for plotting figures are in first created in set_config.py. They are configured to work out-of-the box. We specify the settings first then use others scripts, i.e., FCT.py, BE_BAR.py to generate the figures.

Note: Due to strict export controls, we are unable to provide access to our testbed for evaluating Figure 13. However, if AE reviewers are interested in conducting testbed experiments, we are happy to provide compiled P4 and VMA configurations. Please contact us via HotCRP if you are interested.

Hardware requirements for Figure 13:
	
 Tofino2 Programmable Switches: 3
	
 Linux Servers: At least 2
	
 NICs: Mellanox ConnectX-6
