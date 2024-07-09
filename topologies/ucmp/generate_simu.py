from utils_ucmp import *

import argparse
import pickle
from tqdm import tqdm


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument('--num_tors', type=int, default=108)
    parser.add_argument('--num_ports', type=int, default=6)
    parser.add_argument('--num_slices', type=int, default=18)
    parser.add_argument('--SLICE_E', type=int, default=50)
    parser.add_argument('--LOGIC_PORTION', type=int, default=1)
    parser.add_argument('--MAX_Q_NUM', type=int, default=0)
    parser.add_argument('--alpha', type=int, default=0.5)
    parser.add_argument('--type', type=int, default=5)
    parser.add_argument('--max_workers', type=int, default=10)

    # noinspection PyShadowingNames
    args = parser.parse_args()

    return args


args = parse_arguments()


def generate_simu():
    # Set the path to files
    #read_path = '../run_topology/slice' + \
    #            str(args.SLICE_E) + 'us_portion' + str(args.LOGIC_PORTION) + '_queue' + str(args.MAX_Q_NUM) + '/'
    read_path = './'
    write_path = './'
    suffixes = [
        '_hoho_1path',
        '_hoho_ECMP',
        '_optiroute_paths',
        '_optiroute_adaptive_paths',
        '_optiroute_no_one_hop_paths',
        f'_optiroute_adaptive_paths_alpha_{args.alpha}',
    ]
    T, host, uplink, downlink, reconfig_time = 18, 648, 6, 6, 10000

    #topology = open('./topology/general_from_dynexp_N=108_topK=5.txt', 'r')
    topology = open('./topologies/general_from_dynexp_N=108_topK=1.txt', 'r')
    topology = topology.readlines()

    os.makedirs(write_path, exist_ok=True)
    write_name = 'slice' + str(args.SLICE_E) + 'us_portion' + str(args.LOGIC_PORTION) + \
                 '_queue' + str(args.MAX_Q_NUM) + suffixes[args.type] + '.txt'
    simu_file = open(write_path + write_name, 'w+')

    simu_file.write(str(host) + ' ' + str(uplink) + ' ' + str(downlink) + ' ' + str(108) + '\n')
    simu_file.write(str(T) + ' ' + str(args.SLICE_E * 1000000)
                    + ' ' + str(reconfig_time) + ' ' + str(args.LOGIC_PORTION) + '\n')
    for n in range(0, T):
        simu_file.write(topology[n + 2])

    set_flow_sizes = set()

    for start_slice in tqdm(range(0, T * args.LOGIC_PORTION)):
        read_name = 'slice_' + str(start_slice) + '.txt'
        if args.type == 0:
            set_paths = read_optimal_raw_paths(read_path + 'fully_reconf_one_optimal_path/', read_name)
            pass
        elif args.type == 1:
            set_paths = read_optimal_raw_paths(read_path + 'fully_reconf_all_optimal_paths/', read_name)
            pass
        elif args.type in [2, 3, 4, 5]:
            set_paths = read_all_disjoint_paths(read_path + 'fully_reconf_all_joint_paths/', read_name)
        else:
            raise ValueError('Unexpected type.')

        simu_file.write(str(start_slice) + '\n')
        for ToR_pair in set_paths:
            hop_latency = []
            for hop in list(set_paths[ToR_pair]):
                latency = set_paths[ToR_pair][hop][0]['slice'][-1]
                if len(hop_latency) > 0 and hop_latency[-1][1] == latency:
                    del set_paths[ToR_pair][hop]
                else:
                    hop_latency.append((hop, latency))

            hop_latency_flow = []
            if args.type in [3, 4, 5]:
                for i in range(0, len(hop_latency)):
                    hi, li = hop_latency[i]
                    if i == 0 or hop_latency[i - 1][1] != hop_latency[i][1]:
                        for j in range(i + 1, len(hop_latency)):
                            hj, lj = hop_latency[j]
                            if hop_latency[i][1] != hop_latency[j][1]:
                                flow = 12500 * (li - lj) / (hj - hi) * args.SLICE_E / args.LOGIC_PORTION
                                hop_latency_flow.append([hi, li, flow])
                                break
                    elif hop_latency[i - 1][1] == hop_latency[i][1]:
                        if hop_latency[i - 1][0] < hop_latency[i][0]:
                            raise ValueError('Unexpected latency.')
                        elif hop_latency[i - 1][0] == hop_latency[i][0]:
                            hop_latency_flow.append([hi, li, hop_latency_flow[-1][2]])
                hop_latency_flow.append([hop_latency[-1][0], hop_latency[-1][1], 0])

                for i in range(len(hop_latency_flow) - 3, -1, -1):
                    for j in range(i + 1, len(hop_latency_flow) - 1):
                        if hop_latency_flow[i][2] <= hop_latency_flow[j][2]:
                            diff_h = hop_latency_flow[j + 1][0] - hop_latency_flow[i][0]
                            diff_l = hop_latency_flow[i][1] - hop_latency_flow[j + 1][1]
                            flow = 12500 * diff_l / diff_h * args.SLICE_E / args.LOGIC_PORTION
                            hop_latency_flow[i][2] = max(hop_latency_flow[i][2], flow)

                i = 0
                while i < len(hop_latency_flow):
                    j = i + 1
                    while j < len(hop_latency_flow):
                        if hop_latency_flow[i][2] < hop_latency_flow[j][2]:
                            del set_paths[ToR_pair][hop_latency_flow[j][0]]
                            del hop_latency_flow[j]
                        else:
                            j += 1
                    i += 1

            assert all(l1[1] >= l2[1] for l1, l2 in zip(hop_latency_flow, hop_latency_flow[1:]))
            assert all(l1[2] >= l2[2] for l1, l2 in zip(hop_latency_flow, hop_latency_flow[1:]))

            for k, hop_count in enumerate(set_paths[ToR_pair]):
                if args.type == 4 and len(set_paths[ToR_pair]) > 1 and hop_count == 1:
                    continue
                for pair in set_paths[ToR_pair][hop_count]:
                    if args.type in [3, 4, 5]:
                        set_flow_sizes.add(int(hop_latency_flow[k][2]))
                        simu_file.write(
                            str(ToR_pair[0]) + ' ' + str(ToR_pair[1]) + ' ' + str(int(hop_latency_flow[k][2] / args.alpha))
                        )
                    else:
                        simu_file.write(str(ToR_pair[0]) + ' ' + str(ToR_pair[1]))
                    for hop in range(0, len(pair['path']) - 1):
                        ToR = pair['path'][hop]
                        next_ToR = pair['path'][hop + 1]
                        send_slice = pair['slice'][hop] % (T * args.LOGIC_PORTION)
                        index_line = topology[int(send_slice / args.LOGIC_PORTION) + 2].strip('\n').split(' ')
                        port = int(index_line[6 * ToR: 6 * ToR + 6].index(str(next_ToR))) + 6
                        simu_file.write(' ' + str(port))
                    simu_file.write('\n')
                    for slice in pair['slice']:
                        slice = slice % (T * args.LOGIC_PORTION)
                        simu_file.write(str(slice) + ' ')
                    simu_file.write('\n')

    simu_file.close()

    #with open("./flow_sizes/" + 'slice' + str(args.SLICE_E) + 'us_portion' + str(args.LOGIC_PORTION) + ".pkl", 'wb') as file:
    #    pickle.dump(set_flow_sizes, file)


if __name__ == '__main__':
    generate_simu()
