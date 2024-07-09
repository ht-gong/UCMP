import os


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
                optimal_hop_count = len(paths[0])
                set_paths[(src_ToR, dst_ToR)][optimal_hop_count] = []
                for pair in zip(paths, slices):
                    set_paths[(src_ToR, dst_ToR)][optimal_hop_count].append({'path': pair[0], 'slice': pair[1]})
    file.close()
    return set_paths


def read_all_raw_paths(file_path, file_name):
    set_paths = {}
    with open(file_path + file_name, 'r') as file:
        for _, line in enumerate(file.readlines()):
            line = line.strip().split('\t')
            if len(line) == 1:
                nums = [int(num) for num in line[0].split(' ')]
                assert all(isinstance(num, int) for num in nums)
                if len(nums) == 2:
                    ToR_pair = (nums[0], nums[1])
                    set_paths[ToR_pair] = {}
                elif len(nums) == 1:
                    hop_count = nums[0]
                    set_paths[ToR_pair][hop_count] = []
                else:
                    raise ValueError(f'Unexpected input {nums}')
            else:
                lists = [eval(_list) for _list in line]
                assert all(isinstance(_list, list) for _list in lists)
                paths, slices = lists[0::2], lists[1::2]
                assert all(len(path) == len(paths[0]) for path in paths)
                assert all(len(slice) == len(slices[0]) for slice in slices)
                for pair in zip(paths, slices):
                    set_paths[ToR_pair][hop_count].append({'path': pair[0], 'slice': pair[1]})
    file.close()
    # assert (107, 106) in set_paths
    return set_paths


def read_all_disjoint_paths(file_path, file_name):
    set_paths = {}
    with open(file_path + file_name, 'r') as file:
        start_reading = True
        for _, line in enumerate(file.readlines()):
            line = line.strip()
            if start_reading:
                nums = [int(num) for num in line.split(' ')]
                ToR_pair, max_hop_count = (nums[0], nums[1]), nums[2]
                start_reading, hop_count = False, 1
                set_paths[ToR_pair] = {}
            else:
                if line != 'None':
                    set_paths[ToR_pair][hop_count] = []
                    lists = [eval(_list) for _list in line.split('\t')]
                    assert all(isinstance(_list, list) for _list in lists)
                    paths, slices = lists[0::2], lists[1::2]
                    for pair in zip(paths, slices):
                        set_paths[ToR_pair][hop_count].append({'path': pair[0], 'slice': pair[1]})
                hop_count += 1
            if hop_count > max_hop_count:
                start_reading = True
    file.close()
    return set_paths


def write_all_disjoint_paths(file_path, file_name, set_paths, mode='all'):
    os.makedirs(file_path, exist_ok=True)
    with open(file_path + file_name, 'w+') as file:
        for ToR_pair in set_paths:
            max_hop_count = len(set_paths[ToR_pair].keys())
            file.write(str(ToR_pair[0]) + ' ' + str(ToR_pair[1]) + ' ' + str(max_hop_count) + '\n')
            for hop_count in range(1, max_hop_count + 1):
                if mode == 'all':
                    if set_paths[ToR_pair][hop_count] is not None:
                        for pair in set_paths[ToR_pair][hop_count]:
                            file.write(str(pair['path']) + '\t' + str(pair['slice']) + '\t')
                    else:
                        file.write('None')
                elif mode == 'one':
                    if set_paths[ToR_pair][hop_count] is not None:
                        file.write(str(set_paths[ToR_pair][hop_count][0]['path']) + '\t' +
                                   str(set_paths[ToR_pair][hop_count][0]['slice']) + '\t')
                    else:
                        file.write('None')
                else:
                    raise ValueError('Unexpected saving mode.')
                file.write('\n')
    file.close()
