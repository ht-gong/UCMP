## Default UCMP paths
To generate default UCMP paths (SLICE_E = 50us, alpha = 0.5), try the following workflow:
Create two empty folders /fully_reconf_one_optimal_path, /fully_reconf_all_optimal_paths, and run the following.
``
python3 run_1.py
python3 run_2.py
python3 get_joint_paths.py
python3 generate_simu.py
``

We will generate slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt

## UCMP paths with different time slices
To have UCMP paths with different time slices, change the ``--SLICE_E`` parameter in run_1.py, run_2.py, get_joint_paths.py, and generate_simu.py.

For example, if we run parser.add_argument('--SLICE_E', type=int, default=50) ==> parser.add_argument('--SLICE_E', type=int, default=10), we will generate:

slice10us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt

## UCMP paths with different weighting factor alpha
Change the ``--alpha`` parameter in generate_simu.py

We pre-generated the slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.3.txt/_alpha_0.7.txt, files, as these were used in the paper evaluations.

## UCMP paths with different reconfiguration time
The third number of the second line in slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5.txt represents the reconfiguration time, in units of picoseconds.

To indicate 1us reconfiguration time, change it to 1000000. For 10us, it is 10000000.

We pre-configured slice50us_portion1_queue0_optiroute_adaptive_paths_alpha_0.5_reconfig_1us.txt/reconfig_10us.txt, as these were used in the paper evaluations.