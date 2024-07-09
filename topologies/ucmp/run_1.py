from optiroute import Routing

from tqdm.contrib.concurrent import process_map
import argparse
import os


def parse_arguments():
    parser = argparse.ArgumentParser()

    parser.add_argument('--num_tors', type=int, default=108)
    parser.add_argument('--num_ports', type=int, default=6)
    parser.add_argument('--num_slices', type=int, default=18)
    parser.add_argument('--SLICE_E', type=int, default=50)
    parser.add_argument('--LOGIC_PORTION', type=int, default=1)
    parser.add_argument('--MAX_Q_NUM', type=int, default=0)
    parser.add_argument('--max_workers', type=int, default=18)

    # noinspection PyShadowingNames
    args = parser.parse_args()

    return args


args = parse_arguments()


def read_optimal_raw_paths(file_path, file_name):
    set_paths = {}
    with open(file_path + file_name, 'r') as file:
        for index, line in enumerate(file.readlines()):
            line = line.strip()
            if index % 2 == 0:
                [src_ToR, dst_ToR] = [int(ToR) for ToR in line.split(' ')]
                set_paths[(src_ToR, dst_ToR)] = {}
            else:
                lists = [eval(_list) for _list in line.split('\t')]
                paths, slices = lists[0::2], lists[1::2]
                sorted_pairs = sorted(zip(paths, slices), key=lambda x: len(x[0]))
                optimal_hop_count = len(sorted_pairs[0][1])
                kept_pairs = [pair for pair in sorted_pairs if len(pair[1]) == optimal_hop_count]

                set_paths[(src_ToR, dst_ToR)]['optimal'] = optimal_hop_count
                set_paths[(src_ToR, dst_ToR)][optimal_hop_count] = []
                for pair in kept_pairs:
                    set_paths[(src_ToR, dst_ToR)][optimal_hop_count].append({'path': pair[0], 'slice': pair[1]})
    file.close()
    return set_paths


def write_all_raw_paths(file_path, file_name, set_paths, mode='all'):
    os.makedirs(file_path, exist_ok=True)
    with open(file_path + file_name, 'w+') as file:
        for ToR_pair in set_paths:
            file.write(str(ToR_pair[0]) + ' ' + str(ToR_pair[1]) + '\n')
            #for hop_count in range(1, set_paths[ToR_pair]['optimal'] + 1):
            #    file.write(str(hop_count) + '\n')
            if mode == 'all':
                for pair in set_paths[ToR_pair]:
                    file.write(str(pair['path']) + '\t' + str(pair['slice']) + '\t')
            elif mode == 'one':
                file.write(str(set_paths[ToR_pair][0]['path']) + '\t' +
                            str(set_paths[ToR_pair][0]['slice']) + '\t')
            else:
                raise ValueError('Unexpected saving mode.')
            file.write('\n')
    file.close()


def run(start_time):
    route = Routing()

    # Set the arguments.
    route.SLICE_E = args.SLICE_E * 1000
    route.LOGIC_PORTION = args.LOGIC_PORTION
    route.MAX_Q_NUM = args.MAX_Q_NUM
    route.N = args.num_tors
    route.N_OCS = args.num_ports

    # Set the path to files
    #file_path = str(args.num_tors) + 'tors_' + str(args.num_ports) + 'ports_slice' + \
    #            str(args.SLICE_E) + 'us_portion' + str(args.LOGIC_PORTION) + '_queue' + str(args.MAX_Q_NUM) + '/'
    file_path = './'
    file_name = 'slice_' + str(start_time) + '.txt'

    # Initialize the network.
    route.init_network()
    route.read_opera_scheduling(file_path + 'optical_scheduling/ops_fully_reconf.pkl')
    route.construct_ops()
    route.cal_conn_timing()

    #set_all_paths = read_optimal_raw_paths(file_path + 'fully_reconf_all_optimal_paths/', file_name)
    set_all_paths = {}
    set_one_path = {}
    for i in range(0, route.N):
        for j in range(0, route.N):
            if i == j:
                continue
            set_all_paths[(i, j)] = []
            set_one_path[(i, j)] = []

    for ToR_pair in set_all_paths:
        for n in range(0, 1):
            #route.MAX_HOP = n
            route.init_each_torpair()
            route.crt_time = start_time
            route.src_tor, route.dst_tor = ToR_pair[0], ToR_pair[1]
            route.get_optimal_paths()
            paths, slices = route.optimal_paths, route.optimal_slices
            path, one_slice = route.one_path, route.one_slice

            #for m in range(n, 0, -1):
            kept_pairs = [pair for pair in zip(paths, slices)]
            if len(kept_pairs) > 0:
                set_all_paths[ToR_pair] = []
                for pair in kept_pairs:
                    set_all_paths[ToR_pair].append({'path': pair[0], 'slice': pair[1]})
                    #break
            set_one_path[ToR_pair] = []
            set_one_path[ToR_pair].append({'path': path, 'slice': one_slice})

    write_all_raw_paths(file_path + 'fully_reconf_one_optimal_path/', file_name, set_all_paths, mode='one')
    write_all_raw_paths(file_path + 'fully_reconf_all_optimal_paths/', file_name, set_all_paths, mode='all')


if __name__ == '__main__':
    assert args.num_tors / args.num_ports == args.num_slices

    file_path = str(args.num_tors) + 'tors_' + str(args.num_ports) + 'ports_slice' + \
                str(args.SLICE_E) + 'us_portion' + str(args.LOGIC_PORTION) + '_queue' + str(args.MAX_Q_NUM) + '/'
    #if os.path.isdir(file_path + 'fully_reconf_all_paths/'):
    #    file_lists = os.listdir(file_path + 'fully_reconf_all_paths/')
    #    tasks = [slice for slice in range(0, args.num_slices * args.LOGIC_PORTION) if 'slice_' + str(slice) + '.txt' not in file_lists]
    #else:
    tasks = [slice for slice in range(0, args.num_slices * args.LOGIC_PORTION)]

    print('Length of physical slices: {:.0f} us'.format(args.SLICE_E),
          '\nNumber of logic portions in one physical slice: {}'.format(args.LOGIC_PORTION),
          '\nMaximum number of packets in the queue of each ToR: {}'.format(args.MAX_Q_NUM),
          '\nMaximum number of workers: {}'.format(args.max_workers))

    result = process_map(run, tasks, max_workers=args.max_workers)
