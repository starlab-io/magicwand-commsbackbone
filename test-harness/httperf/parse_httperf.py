#!/usr/bin/python
import sys
import json
import datetime

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

def dump_samples_json(samples, filename):
    """
    While the data may be fairly basic, we can still dump to a JSON file
    for usability. The output looks roughly like:

        {
            "samples": [...],
            "collated_at": <collation timestamp>,
            "min_timestamp": "MINIMUM OBSERVED TIMESTAMP",
            "max_timestamp": "MAXIMUM OBSERVED TIMESTAMP"
        }

    :param samples:
    :param filename:
    :return:
    """

    with open(filename, "w") as file:
        json.dump(
            {
                "samples": samples,
                "collated_at": str(datetime.datetime.now()),
                "min_timestamp": samples[0][0],
                "max_timestamp": samples[-1][0]
            },
            file
        )

def dump_samples_csv(samples, filename):
    """
    Output the samples of the reply rate to the given file. We're hand-jamming CSV
    because at this point we know the exact format of the data. This can only cause
    problems in the future.

    :param filename: Output file for performance data
    :return: None
    """
    with open(filename, 'w') as file:
        file.write("mm:ss elapsed,reply rate\n")
        
        for sample in samples:

            # for output we're formatting in M:S
            minute = sample[0] / 60
            second = sample[0] % 60
            tfmt = "%02d:%02d" % (minute, second)
            file.write("%s,%s\n" % (tfmt, sample[1]))

if __name__ == "__main__":

    input_file = "/var/log/httperf/performance.log"
    csv_output_file = "/var/log/httperf/performance.csv"
    json_output_file = "/var/log/httperf/performance.json"

    # poor mans arg parse
    if len(sys.argv) > 1:
        input_file = sys.argv[1]

    if len(sys.argv) > 2:
        csv_output_file = sys.argv[2]

    if len(sys.argv) > 3:
        json_output_file = sys.argv[3]

    samples = parse_samples(input_file)
    dump_samples_csv(samples, csv_output_file)
    dump_samples_json(samples, json_output_file)