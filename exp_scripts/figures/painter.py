import argparse
import os
import re

import numpy as np

arg_parser = argparse.ArgumentParser(
    description=''' The common painter for experiment result ''')
arg_parser.add_argument(
    '-i', '--input', default=[], nargs='+',
    help='The input directory list. Each directory is one curve in the figure')

arg_parser.add_argument(
    '-o', '--output', type=str, default='result',
    help='Output figure name')
arg_parser.add_argument(
    '--xfactor', default=[], nargs='+',
    help='x-axis multi-factor')

args = arg_parser.parse_args()

input_dirs = list(args.input)
output_path = os.getcwd() + '/' + str(args.output) + '.png'
x_factor = len(args.xfactor)


def parse_line(line):
    machine_pattern = re.compile(r"^@(.*?)\s.*?$")

    thpt_pattern = re.compile(r"^.*?thpt:\s(.*?)\sreqs/sec.*?$")

    lat_pattern = re.compile(r"^.*?epoch\.\s(.*?)\sus")
    if thpt_pattern.match(line) is None:
        return None, None, None
    machine = machine_pattern.findall(line)[-1]
    thpt = float(thpt_pattern.findall(line)[-1])
    lat = float(lat_pattern.findall(line)[-1])
    return machine, thpt, lat


def analyse(file_path):
    exit_pattern = re.compile(r"^.*?exit.*?$")
    with open(file_path) as f:
        thpt_hash = {}
        lat_hash = {}
        for line in f:
            if exit_pattern.match(line) is not None: break
            machine, thpt, lat = parse_line(line)
            if machine is None: continue
            if machine not in thpt_hash.keys():
                thpt_hash[machine] = []
            if machine not in lat_hash.keys():
                lat_hash[machine] = []
            thpt_hash[machine].append(thpt)
            lat_hash[machine].append(lat)

        thpt_avg = {}
        lat_avg = {}
        for machine, thpts in thpt_hash.items():
            thpt_avg[machine] = np.mean(np.sort(thpts)[1:5])
        for machine, lats in lat_hash.items():
            lat_avg[machine] = np.mean(np.sort(lats)[-5:-1])

    sum_up_thpt = sum(list(thpt_avg.values()))
    avg_lat = np.mean(list(lat_avg.values()))
    return sum_up_thpt, avg_lat


color_list = ["#268BD2", "#2AA198", "#859900", "#B58900", "#CB4B16", "#DC322F", "#D33682", "#6C71C4"]


def plot_fig(fig_name, thpt_lat_dict_map):
    import matplotlib.pyplot as plt
    plt.style.use("seaborn-bright")
    for i, directory in enumerate(list(thpt_lat_dict_map.keys())):
        color = color_list[i]
        title = directory.split('/')[-1]
        if '.' in title:
            title = title.split('.')[-1]
        thpt_lat_dict = thpt_lat_dict_map[directory]
        keys = list(sorted(dict(thpt_lat_dict).keys()))
        thpts = [thpt_lat_dict[key]['thpt'] for key in keys]
        lats = [thpt_lat_dict[key]['lat'] for key in keys]

        plt.subplot(3, 1, 1)
        if 'connect' not in title:
            plt.xlabel('Throughput (op/s)')
            plt.ylabel('latency (us)')
            plt.plot(thpts, lats, label=title, color=color)
        else:
            t = [np.log10(x) for x in thpts]
            l = [np.log10(x) for x in lats]
            plt.xlabel('Throughput ($10^x$ op/s)')
            plt.ylabel('latency ($10^y$ us)')
            plt.plot(t, l, label=title, color=color)

        plt.legend()
        plt.title('Throughput-Latency')
        plt.subplot(3, 1, 2)

        plt.xlabel('Thread Number')
        plt.ylabel('Throughput (op/s)')
        plt.plot(keys, thpts, label=title, color=color)
        plt.legend()

        plt.title('Thread-Throughput')
        plt.subplot(3, 1, 3)
        if 'connect' not in title:
            plt.plot(keys, lats, label=title, color=color)
            plt.ylabel('latency (us)')
        else:
            plt.plot(keys, [np.log10(x) for x in lats], label=title, color=color)
            plt.ylabel('latency ($10^y$ us)')
        plt.xlabel('Thread Number')
        plt.legend()
        plt.title('Thread-Latency')
    plt.tight_layout()
    plt.savefig(fig_name)
    print('figure has been stored into', fig_name)


def main(input_dir_list, output):
    dic_map = {}

    for directory in input_dir_list:
        for root, dirs, files in os.walk(directory):
            dic = {}
            for f in files:
                pattern = re.compile(r"^run-.*?toml$")
                if pattern.match(f) is not None:
                    idx = int(re.findall(r"^run-(.*?)\.toml$", f)[-1])
                    log_path = "{}/{}.txt".format(directory, f)
                    thpt, lat = analyse(log_path)
                    dic[idx * x_factor] = {'thpt': thpt, 'lat': lat}
                    dic_map[directory] = dic
    plot_fig(output, dic_map)


if __name__ == '__main__':
    main(input_dirs, output_path)
