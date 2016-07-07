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

    In an attempt to get accurate time stamps, the start of the performance log file will
    have an entry that looks like:

        start-time 2016-07-06T14:20:21

    We use this as the base time in our calculations. Given that, we increment the time
    by five seconds for each parsed "reply-rate" entry. We can assume this time interval
    because of the _RATE_INTERVAL_ sampling log rate defined in
    the [httperf source](https://github.com/httperf/httperf/blob/cc888437e4572ec29a4a7209f34fbd39c31600f5/src/httperf.c#L90)

    :param filename: HTTPerf log file
    :return: A list of 2-tuples, each tuple being the sample time and throughput rate.
    """
    samples = []
    test_duration = 0
    time = 0
    basetime = datetime.datetime.now()

    with open(filename) as file:
        for line in file:
            if line.startswith("start-time"):
                basetime_string = line.strip().split(" ")[1]

                # convert this to a usable time object
                basetime = datetime.datetime.strptime(basetime_string, "%Y-%m-%dT%H:%M:%S")

            elif line.startswith("reply-rate"):
                rate = line.strip().split(" = ")[1]
                time += 5
                replyrate_time = basetime + datetime.timedelta(seconds=time)
                samples.append((replyrate_time.strftime("%Y-%m-%dT%H:%M:%S"), rate))
            elif line.startswith("test-duration"):
                test_duration = int(line.strip().split(" ")[1])

    return test_duration, samples


def dump_samples_json(test_duration, samples, filename):
    """
    While the data may be fairly basic, we can still dump to a JSON file
    for usability. The output looks roughly like:

        {
            "samples": [...],
            "collated_at": <collation timestamp>,
            "min_timestamp": "MINIMUM OBSERVED TIMESTAMP",
            "max_timestamp": "MAXIMUM OBSERVED TIMESTAMP",
            "test_duration": #
        }

    :param test_duration:
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
                "max_timestamp": samples[-1][0],
                "test_duration": test_duration
            },
            file
        )


def dump_samples_csv(test_duration, samples, filename):
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

            # the only change we're introducing to the sample data is to force
            # a timezone via the Z block
            file.write("%sZ,%s\n" % sample)

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

    test_duration, samples = parse_samples(input_file)
    dump_samples_csv(test_duration, samples, csv_output_file)
    dump_samples_json(test_duration, samples, json_output_file)