from collections import defaultdict
import matplotlib.pyplot as plt
import numpy as np
import pickle


N_ToR = 108
N_downlink = 6
N_uplink = 6
queue_size = 300
packet_size = 1500
st1 = 300000
st2 = 500000
interval = 20


def get_bin_and_cdf(arr):
    cnt, b_cnt = np.histogram(arr, bins=10000)
    pdf = cnt / sum(cnt)
    cdf = np.cumsum(pdf)
    return b_cnt, cdf


with open('loading/config.pickle', "rb") as file:
    config = pickle.load(file)

folder_name, file_names, labels = config["folder_name"], config["file_names"], config["labels"]
colors, markers, endpoint = config["colors"], config["markers"], config["endpoint"]

set_times, set_tor_host_util, set_tor_tor_util = [], [], []

for i in range(len(file_names)):
    with open('./' + folder_name + file_names[i] + '.txt', 'r') as file:
        pipes = defaultdict(list)
        times = np.zeros(0)
        for line in file.readlines():
            line = line.split()
            if len(line) <= 0:
                continue
            if line[0] == "Pipe":
                tor = int(line[1])
                port = int(line[2])
                pipes[(tor, port)].append(float(line[3]))
            if line[0] == "Util":
                if line[1] != 'Curtime':
                    times = np.append(times, [float(line[3])])

        assert len(pipes[(0, 6)]) == len(times)
        tor_host_util = np.zeros(len(times))
        tor_tor_util = np.zeros(len(times))
        print(f"{labels[i]}. Total samples: {len(times)}")
        for t in range(len(times)):
            arr = []
            for tor in range(N_ToR):
                for port in range(0, N_downlink):
                    arr.append(pipes[(tor, port)][t])
            tor_host_util[t] = sum(arr) / (N_ToR * N_downlink)

            arr = []
            for tor in range(N_ToR):
                for port in range(N_downlink, N_downlink + N_uplink):
                    arr.append(pipes[(tor, port)][t])
            tor_tor_util[t] = sum(arr) / (N_ToR * N_uplink)
        
        set_times.append(times)
        set_tor_host_util.append(tor_host_util)
        set_tor_tor_util.append(tor_tor_util)

for ftype in [1, 2, 3, 4]:  
    fig, ax = plt.subplots()
    legends = []
    
    for i in range(len(file_names)):
        times, tor_host_util, tor_tor_util = set_times[i], set_tor_host_util[i], set_tor_tor_util[i]

        end = int(np.where(times == endpoint)[0] + 1)
        marker = next(markers)
        if ftype == 1:
            ax.plot(times[:end], tor_host_util[:end],
                    color=colors[i], marker=marker, linestyle='', markersize=3)
            line, = ax.plot(0, -1, color=colors[i], marker=marker, label=labels[i], linestyle='', markersize=10)
        elif ftype == 2:
            ax.plot(times[:end], tor_tor_util[:end],
                    color=colors[i], marker=marker, linestyle='', markersize=3)
            line, = ax.plot(0, -1, color=colors[i], marker=marker, label=labels[i], linestyle='', markersize=10)
        elif ftype == 3:
            tor_host_bins, tor_host_cdf = get_bin_and_cdf(tor_host_util)
            line, = ax.plot(tor_host_bins[1:], tor_host_cdf,
                            color=colors[i], label=labels[i], linestyle='-', markersize=0, linewidth=3)
        elif ftype == 4:
            tor_tor_bins, tor_tor_cdf = get_bin_and_cdf(tor_tor_util)
            line, = ax.plot(tor_tor_bins[1:], tor_tor_cdf,
                            color=colors[i], label=labels[i], linestyle='-', markersize=0, linewidth=3)

        legends.append(line)

    if ftype == 1:
        ax.set_xlabel("Time ($ms$)", fontsize=26)
        ax.set_ylabel("Avg. link util. (ToR to host)", fontsize=21)
    elif ftype == 2:
        ax.set_xlabel("Time ($ms$)", fontsize=26)
        ax.set_ylabel("Avg. ToR-to-ToR link util.", fontsize=23)
    elif ftype == 3:
        ax.set_xlabel("Avg. link util. (ToR to host)", fontsize=21)
        ax.set_ylabel("CDF", fontsize=26)
    elif ftype == 4:
        ax.set_xlabel("Avg. link util. (ToR to ToR)", fontsize=21)
        ax.set_ylabel("CDF", fontsize=26)

    if ftype in [1, 2]:
        margin = endpoint / 20
        ax.set_xlim([-margin, endpoint + margin])
        step = endpoint / 4
        ax.set_xticks(np.arange(0, endpoint + step, step))
    elif ftype in [3, 4]:
        ax.set_xlim(-0.05, 1.05)
        ax.set_xticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])

    ax.set_ylim(-0.05, 1.05)
    ax.set_yticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])
    ax.tick_params(axis='x', labelsize=24)
    ax.tick_params(axis='y', labelsize=24)

    plt.legend(handles=legends, loc='best', fontsize=24, frameon=False)

    plt.grid(ls='--')

    if ftype == 1:
        plt.savefig(f'./figures/avg_link_util_tor_host.{config["suffix"]}', bbox_inches='tight')
    elif ftype == 2:
        plt.savefig(f'./figures/avg_link_util_tor_tor.{config["suffix"]}', bbox_inches='tight')
    elif ftype == 3:
        plt.savefig(f'./figures/avg_link_util_tor_host_cdf.{config["suffix"]}', bbox_inches='tight')
    elif ftype == 4:
        plt.savefig(f'./figures/avg_link_util_tor_tor_cdf.{config["suffix"]}', bbox_inches='tight')

