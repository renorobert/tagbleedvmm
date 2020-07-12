import numpy
import matplotlib.pyplot as plt
from scipy.signal import medfilt
from pandas import DataFrame
import operator
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("filename", help = "provide path to trace file")
args = parser.parse_args()

data = numpy.genfromtxt(args.filename, delimiter = ",", dtype = None, 
			usecols = (1), skip_header = 11, invalid_raise = False)

rounds = 1000
sets = data.size/rounds

tlb_n_timing = {}
tlb_list = []
minval = maxval = 0

for tlb_set in range(sets):
	sample = data[tlb_set * rounds: (tlb_set + 1) * rounds]
        sample = medfilt(sample, rounds - 1)
        tlb_list.append(sample)
	avg = sum(sample.tolist())/rounds
	tlb_n_timing[tlb_set] = avg

g_index = [s for s in range(sets)]
g_cols  = [r for r in range(rounds)]
df = DataFrame(tlb_list, index = g_index, columns = g_cols)

plt.pcolor(df)
plt.savefig("results/trace_tlb.png")

sorted_tlb_timing = sorted(tlb_n_timing.items(), key = operator.itemgetter(1))[::-1]

for tlb_timing in sorted_tlb_timing:
        tlb_set, time = tlb_timing
        print "Set: 0x%02x, Time: %d" % (tlb_set, time)
