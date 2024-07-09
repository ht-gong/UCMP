import math
import matplotlib.pyplot as plt
import numpy as np
import pickle

with open('loading/config.pickle', "rb") as file:
    config = pickle.load(file)

folder_name, file_names, labels = config["folder_name"], config["file_names"], config["labels"]
colors, markers = config["colors"], config["markers"]

fig, ax = plt.subplots(nrows=1)
legends, BE = [], {}
for i in range(len(file_names)):
    flow_sizes = []
    hop_counts = []
    band_efficiencies = []
    with open('./' + folder_name + file_names[i] + '.txt', 'r') as file:
        for line in file.readlines():
            line = line.split()
            if len(line) <= 0:
                continue
            if line[0] == 'FCT' and len(line) == 12:
                flow_sizes.append(math.ceil(int(line[3]) / 1436))
                hop_counts.append(int(line[11]))
                band_efficiencies.append(flow_sizes[-1] / hop_counts[-1])

    x = sorted(band_efficiencies)
    y = np.array(range(1, len(x) + 1)) / float(len(x))

    marker = None
    line, = plt.plot(x, y, color=colors[i], label=labels[i], linestyle='-', marker=marker, linewidth=3)
    legends.append(line)

    BE[labels[i]] = sum(flow_sizes) / sum(hop_counts)
    print('\'' + labels[i] + '\':', '{:.3f},'.format(BE[labels[i]]))

with open('loading/BE.pickle', 'wb') as file:
    pickle.dump(BE, file)

plt.xlabel('Band efficiency', fontsize=26)
plt.ylabel('CDF', fontsize=26)
plt.legend(handles=legends, loc='lower right', fontsize=18)

ax.set_xlim([-0.05, 1.05])
ax.set_ylim([-0.05, 1.05])

ax.set_xticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])
ax.set_yticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])
ax.tick_params(axis='x', labelsize=24)
ax.tick_params(axis='y', labelsize=24)

plt.grid(ls='--')
plt.savefig(f'./figures/BE_CDF.{config["suffix"]}', bbox_inches='tight')
