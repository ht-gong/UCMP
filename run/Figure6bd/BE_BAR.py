import numpy as np
import matplotlib.pyplot as plt
import pickle

species = ("50",)
with open('loading/BE.pickle', 'rb') as file:
    penguin_means = pickle.load(file)
for key in penguin_means:
    print("{}: {:.4f}".format(key, penguin_means[key]))

with open('loading/config.pickle', "rb") as file:
    config = pickle.load(file)
colors = config["colors"]

x = np.arange(len(species))  # the label locations
width = 0.15  # the width of the bars
multiplier = 0

fig, ax = plt.subplots()
plt.grid(axis='y', zorder=0, ls='--')

for attribute, measurement in penguin_means.items():
    offset = 1.2 * width * multiplier
    rects = ax.bar(x + offset, measurement,
                   width=width, label=attribute, color=colors[multiplier], zorder=3, align='center')
    multiplier += 1

ax.set_xlabel('Routing solutions', fontsize=30, color='black')
ax.set_ylabel('Bandwidth efficiency', fontsize=26)

if config["legend"]:
    ax.legend(loc='upper right', fontsize=18)

ax.set_ylim(-0.05, 1.05)

# ax.set_xticks(x + width * 1.2)
ax.set_xticklabels(species)
# ax.tick_params(axis='x', labelsize=24)
ax.tick_params(axis='y', labelsize=24)
ax.set_xticks(x)
ax.tick_params(axis='x', labelsize=18, labelcolor='white')

plt.savefig(f'./figures/BE.{config["suffix"]}', bbox_inches='tight')
