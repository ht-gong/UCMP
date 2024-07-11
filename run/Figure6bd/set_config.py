import itertools
import os
import pickle
import shutil
import sys

end_point = int(float(sys.argv[1])) #first argument is end_time
end_point = min(end_point, 500)
file_names = sys.argv[2:]

palette = {
    (1, 1): [231, 215, 188], (1, 2): [250, 225, 221], (1, 3): [215, 179, 137], (1, 4): [219, 227, 238],
    (2, 1): [7, 24, 75], (2, 2): [104, 122, 164], (2, 3): [79, 88, 105],
    (3, 1): [238, 78, 66], (3, 2): [172, 58, 58], (3, 3): [114, 56, 37],
    (4, 1): [6, 85, 58], (4, 2): [204, 213, 105],
    (5, 1): [190, 201, 64],
}
google = {(1, 1) : [66, 133, 244], (1, 2) : [219, 68, 55], (1, 3) : [244, 180, 0], (1, 4) : [15, 157, 88], (1, 5) : [115, 115, 115], (1, 6) : [0, 0, 0], (1, 7) : [255, 0, 191]}
for index in google:
    google[index] = [value / 255 for value in google[index]]
for index in palette:
    palette[index] = [value / 255 for value in palette[index]]
colors = [
    google[(1, 1)], 
    google[(1, 2)],
    google[(1, 3)],
    google[(1, 5)], 
    google[(1, 6)], 
    google[(1, 4)],
    google[(1, 7)],
]
markers = itertools.cycle(('o', '^', 's', 'd', 'p', 'v'))

config = {
    "folder_name": "",
    "file_names": file_names,
    "labels": [
        r'UCMP + DCTCP',
        r'UCMP + NDP',
        r'VLB',
        r'KSP ($k$ = 1)',
        r'KSP ($k$ = 5)',
        r'Opera ($k$ = 1)',
        r'Opera ($k$ = 5)'
    ],
    "colors": colors,
    "markers": markers,
    "endpoint": end_point,
    "suffix": "pdf",
    "legend": True
}

if os.path.isdir("./figures"):
    shutil.rmtree("./figures")
if os.path.isdir("./loading"):
    shutil.rmtree("./loading")
os.makedirs("./figures", exist_ok=False)
os.makedirs("./loading", exist_ok=False)

with open('loading/config.pickle', 'wb') as file:
    pickle.dump(config, file)
