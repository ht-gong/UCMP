from utils_ucmp import *

from tqdm.contrib.concurrent import process_map
import argparse


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument('--num_tors', type=int, default=108)
    parser.add_argument('--num_ports', type=int, default=6)
    parser.add_argument('--num_slices', type=int, default=18)
    parser.add_argument('--SLICE_E', type=int, default=50)
    parser.add_argument('--LOGIC_PORTION', type=int, default=1)
    parser.add_argument('--MAX_Q_NUM', type=int, default=0)
    parser.add_argument('--max_workers', type=int, default=10)

    # noinspection PyShadowingNames
    args = parser.parse_args()

    return args


args = parse_arguments()


def disjoint_paths(start_time):
    # Set the path to files
    #file_path = '../run_topology/slice' + \
    #            str(args.SLICE_E) + 'us_portion' + str(args.LOGIC_PORTION) + '_queue' + str(args.MAX_Q_NUM) + '/'
    file_path = './'   
    
    # file_path = '../run_topology/hoho_8tors_4ports_50us_1us/'
    file_name = 'slice_' + str(start_time) + '.txt'

    set_paths = read_all_raw_paths(file_path + 'fully_reconf_all_paths/', file_name)

    for ToR_pair in set_paths:
        for hop_count in set_paths[ToR_pair]:
            pairs, num_pairs = set_paths[ToR_pair][hop_count], len(set_paths[ToR_pair][hop_count])
            if len(pairs[0]['slice']) == hop_count:
                # if num_pairs > 1:
                #     n = 0
                #     while n < num_pairs:
                #         delete_indices = set()
                #         for i in range(1, hop_count):
                #             mid_ToR = pairs[n]['path'][i]
                #             for m in range(n + 1, num_pairs):
                #                 if mid_ToR in pairs[m]['path']:
                #                     delete_indices.add(m)
                #         pairs = [pair for index, pair in enumerate(pairs) if index not in delete_indices]
                #         n += 1
                #         num_pairs -= len(delete_indices)
                #
                # union_paths = []
                # for pair in pairs:
                #     union_paths += pair['path']
                # assert len(set(union_paths)) == (hop_count - 1) * len(pairs) + 2

                set_paths[ToR_pair][hop_count] = pairs
            else:
                set_paths[ToR_pair][hop_count] = None

    # write_all_disjoint_paths(file_path + 'fully_reconf_all_disjoint_paths/', file_name, set_paths, 'all')
    write_all_disjoint_paths(file_path + 'fully_reconf_all_joint_paths/', file_name, set_paths, 'all')


if __name__ == '__main__':
    assert(args.num_slices == args.num_tors / args.num_ports)
    tasks = [slice for slice in range(0, args.num_slices * args.LOGIC_PORTION)]
    result = process_map(disjoint_paths, tasks, max_workers=args.max_workers)
