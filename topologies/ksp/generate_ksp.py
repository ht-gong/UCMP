from utils import *

import numpy as np
from tqdm import tqdm

import argparse
import pickle
import os


def parse_arguments():
    parser = argparse.ArgumentParser()

    parser.add_argument('--N', type=int, default=108)
    parser.add_argument('--p', type=int, default=12)
    parser.add_argument('--k', type=int, default=1)

    # noinspection PyShadowingNames
    args = parser.parse_args()

    return args


# noinspection PyShadowingNames
def generate_simu(args):
    read_path = f'./topo/expander_N=108_p=12_k={args.k}/'
    write_path = ''

    #topology = open('topology/old_from_dynexp_N=108_topK=5.txt', 'r')
    topology = open('dyn_topo.txt', 'r')
    topology = topology.readlines()

    os.makedirs(write_path, exist_ok=True)
    write_name = f'general_from_dynexp_N={args.N}_topK={args.k}.txt'
    simu_file = open(write_path + write_name, 'w+')

    for n in range(0, 20):
        simu_file.write(topology[n])

    for slice in tqdm(range(18)):
        with open(read_path + f'index={slice}.pkl', 'rb') as file:
            set_paths = pickle.load(file)

        simu_file.write(str(slice) + '\n')
        for ToR_pair in set_paths:
            for n, path in enumerate(set_paths[ToR_pair]['ToRs']):
                if n < args.k:
                    simu_file.write(str(ToR_pair[0]) + ' ' + str(ToR_pair[1]))
                    for hop in range(0, len(path) - 1):
                        ToR = path[hop]
                        next_ToR = path[hop + 1]
                        index_line = topology[slice + 2].strip('\n').split(' ')
                        port = int(index_line[6 * ToR: 6 * ToR + 6].index(str(next_ToR))) + 6
                        simu_file.write(' ' + str(port))
                    simu_file.write('\n')


if __name__ == '__main__':
    args = parse_arguments()
    generate_simu(args)
