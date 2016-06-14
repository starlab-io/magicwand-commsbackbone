#!/usr/bin/python
import sys

def parse_samples(filename):
    """
    Given a filename, parse out the reply-rate samples as output by the HTTPerf --verbose
    performance benchmark. Given that HTTPerf samples at 5 second intervals, we can infer
    the rough timestamps (from a time of 0), which we'll create and track along with the
    sample rate

    :param filename: HTTPerf log file
    :return: A list of 2-tuples, each tuple being the sample time and throughput rate.
    """
    samples = []
    time = 0

    with open(filename) as file:
        for line in file:
            if line.startswith("reply-rate"):
                rate = line.strip().split(" = ")[1]
                time += 5
                samples.append((time, rate))
    return samples

def dump_samples(samples, filename):
    """
    Output the samples of the reply rate to the given file. We're hand-jamming CSV
    because at this point we know the exact format of the data. This can only cause
    problems in the future.

    :param filename: Output file for performance data
    :return: None
    """
    with open(filename, 'w') as file:
        for sample in samples:

            # for output we're formatting in M:S
            minute = sample[0] / 60
            second = sample[0] % 60
            tfmt = "%02d:%02d" % (minute, second)
            file.write("%s,%s\n" % (tfmt, sample[1]))

if __name__ == "__main__":

    input_file = "/var/log/httperf/performance.log"
    output_file = "/var/log/httperf/performance.csv"

    # poor mans arg parse
    if len(sys.argv) > 1:
        input_file = sys.argv[1]

    if len(sys.argv) > 2:
        output_file = sys.argv[2]

    samples = parse_samples(input_file)
    dump_samples(samples, output_file)