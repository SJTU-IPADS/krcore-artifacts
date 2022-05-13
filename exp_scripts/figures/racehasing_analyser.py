import argparse
import os
import re

import numpy as np

arg_parser = argparse.ArgumentParser(
    description=''' The common painter for experiment result ''')
arg_parser.add_argument(
    '-i', '--input', type=str,
    help='The input race hashing directory')

arg_parser.add_argument(
    '-o', '--output', type=str, default='result',
    help='Output figure name')

args = arg_parser.parse_args()

input_path = str(args.input)
output_path = os.getcwd() + '/' + str(args.output) + '.png'


def parse_line(line):
    machine_pattern = re.compile(r"^@(.*?)\s.*?$")

    thpt_pattern = re.compile(r"^.*?thpt:\s(.*?)\sreqs/sec.*?$")

    epoch_pattern = re.compile(r"^.*?epoch\s@(.*?):.*?$")
    if thpt_pattern.match(line) is None or epoch_pattern.match(line) is None:
        return None, None
    thpt = float(thpt_pattern.findall(line)[-1])
    epoch = int(epoch_pattern.findall(line)[-1])
    return thpt, epoch


def analyse(file_path):
    idx = 0
    trigger_line = 0
    with open(file_path) as f:
        thpts = []
        epochs = []
        for line in f:
            thpt, epoch = parse_line(line)
            if 'Trigger' in line: trigger_line = idx
            if thpt is None or thpt == 0: continue
            thpts.append(thpt)
            epochs.append(epoch)
            idx += 1

        data = [epochs, thpts]
        data = np.transpose(data)
        # df = pd.DataFrame(data, columns=['epoch', 'thpt'])
        # df.to_excel('race-hasing.xlsx')
        for i in range(5):
            maxindex = np.argmax(thpts)
            thpts = np.delete(thpts, maxindex)
            epochs = np.delete(epochs, maxindex)

        return epochs, thpts, trigger_line


from matplotlib import pyplot as plt

fontsize = 7

if __name__ == '__main__':
    M = 1000 * 1000
    x, y, t0 = analyse('%s/race-hashing-krcore/run-krcore-race-hashing.toml.txt' % input_path)
    x_async, y_async, t1 = analyse('%s/race-hashing-krcore-async/run-krcore-race-hashing-async.toml.txt' % input_path)
    x_verbs, y_verbs, t2 = analyse('%s/race-hashing-verbs/run-verbs-race-hashing.toml.txt' % input_path)

    y = [t / M for t in y]
    y_async = [t / M for t in y_async]
    y_verbs = [t / M for t in y_verbs]
    linewidth = 0.5

    fig, ax = plt.subplots()

    # plot lines
    ax.plot(x, y, label='KRCore', markerfacecolor='none', marker=None, linestyle="solid", color='blue',
            markeredgewidth=linewidth, linewidth=linewidth)
    ax.plot(x_async, y_async, label='KRCore (async)', markerfacecolor='none', marker=None, linestyle="solid",
            color='red',
            markeredgewidth=linewidth, linewidth=linewidth)
    ax.plot(x_verbs, y_verbs, label='Verbs', markerfacecolor='none', marker=None, linestyle="solid", color='green',
            markeredgewidth=linewidth, linewidth=linewidth)

    ax.set_xlabel('Timeline (ms)', fontsize=fontsize + 2, labelpad=-0.1)
    ax.set_ylabel('Throughput (M op/s)', fontsize=fontsize + 2, labelpad=1)
    plt.legend()
    # plt.show()
    plt.savefig(output_path)
