#/usr/bin/python
import sys
import json
import datetime
from collections import OrderedDict

"""
We're going to be pulling apart the logs of GoLoris to build a usable data set. To do this we need to
work around some problems in Go based logging (default timestamps on logs are at the second level, with
no easy way to change this) and build a data structure that's useful for visualization as well as
analysis. To this end, we're building a standard CSV row based data set, as well as a compound JSON
based set that can be easily leveraged for viz and analysis.

The CSV version will have the raw filtered logs of connections.

The JSON version will have two embedded data sets

    - A set of the raw filtered logs of connections
    - An aggregate (at the second level) data set for Viz
"""


def parse_samples(filename):
    """
    Given a log file, parse out the samples into a row/dict based structure, and get any relevant
    metadata output by GoLoris.

    :param filename: File path of the GoLoris log
    :return: (metadata, Row based data of (timestamp, connections))
    """
    metadata = {}
    samples = []

    with open(filename, 'r') as file:
        for line in file:

            if line.startswith("20"):
                # This is a log message we might want
                # Hacky, yes, but it'll work for the next 84 years.
                bits = line.strip().split(" ")
                if bits[2] == "Holding":
                    # this is a connection status message, let's keep it
                    # but first transform the time stamp from
                    #       YYYY/MM/DD HH:MM:SS
                    # to
                    #       YYYY-MM-DDTHH:MM:SSZ

                    fmt_date = bits[0].replace("/", "-") + "T" + bits[1] + "Z"
                    samples.append( (fmt_date, int(bits[3])) )
            elif "=" in line:
                # metadata line? Sure.
                bits = line.strip().split("=")
                metadata[bits[0]] = bits[1]
    return metadata, samples


def dump_samples_csv(samples, filename):
    """
    Output the sample set to a CSV file. We're hand-jamming this because we have strict control
    over the content of the data set. Why don't we use a CSV writer? Because I'm lazy.

    :param samples: row/tuple sets of (timestamp, connections)
    :param filename: File path to write CSV
    :return: None
    """

    with open(filename, "w") as file:
        # header
        file.write("timestamp,connections\n")

        # rows
        for sample in samples:
            file.write("%s,%s\n" % sample)


def dump_samples_json(metadata, aggregate, samples, filename):
    """
    Create the JSON structure of two parallel data sets; one the raw set as it was written
     to CSV, the other a row based structure aggregated at the minute level with the following
     values:
        {
            "timestamp": <time>
            "mix": <min # of connections>
            "mean": <mean # of connections>
            "max": <max # of connections>
        }

    We also write a metadata section, and some high level sample information, so our top level
    structure looks like:

        {
            "samples":[raw samples],
            "aggregate": [aggregate set],
            "metadata": { metadata k,v },
            "test_duration": seconds of testing,
            "min_timestamp": "min time",
            "max_timestamp": "max time",
            "collated_at": collation/aggregate time

        }

    :param metadata: GoLoris metadata for this sample set
    :param aggregate: Aggregate data set (at second level)
    :param samples: Raw filtered data set
    :param filename: JSON file to which data is written
    :return:
    """

    with open(filename, "w") as file:
        json.dump(
            {
                "samples": samples,
                "aggregate": aggregate,
                "metadata": metadata,
                "test_duration": lorisTimeToSeconds(metadata["testDuration"]),
                "min_timestamp": samples[0][0],
                "max_timestamp": samples[-1][0],
                "collated_at": str(datetime.datetime.now())
            },
            file
        )
    return None


def lorisTimeToSeconds(ltime):
    """
    Because who the hell knows why, GoLoris reports it's test duration in format that looks like:

        1h0m02

    So we need to convert what ever that looks like into seconds.

    EDIT --- Oh fun. This duration time format will elide parts of the time if they aren't relevant, so
            an 80 second test will get the duration stamp of _1m20s_, likewise a 20 second test will
            be printed as _20s_.

    :param ltime: String loris formatted time
    :return: Integer of seconds
    """
    bits = []
    tail = ltime
    splits = ["h", "m", "s"]
    for splitter in splits:
        if splitter in ltime:
            spl = tail.split(splitter)
            bits.append(int(spl[0]))
            tail = spl[1] if len(spl) > 1 else ""
    # work up from reverse while there are still values to be had
    mults = [1, 60, 3600]
    seconds = 0
    while len(bits) > 0:
        seconds += mults.pop(0) * bits.pop()
    return seconds


def aggregate_samples(samples):
    """
    Generate the aggregate data set from the sample list.

    :param samples: Aggregated data structure (ordered list of dicts)
    :return:
    """

    # first we need to bucket everything. We'll need FIFO ordering when
    # we retrieve values (to maintain time monotonicity)
    time_buckets = OrderedDict()

    for sample in samples:
        if sample[0] not in time_buckets:
            time_buckets[sample[0]] = []
        time_buckets[sample[0]].append(sample[1])

    # now we can iterate and build our final aggregate values
    agg_buckets = []

    for timestamp, subsample in time_buckets.items():
        # we're using a floored average
        agg_buckets.append(
            {
                "timestamp": timestamp,
                "min": min(subsample),
                "max": max(subsample),
                "avg": sum(subsample) / len(subsample)
            }
        )
    return agg_buckets


if __name__ == "__main__":

    input_file = "/var/log/goloris/performance.log"
    csv_output_file = "/var/log/goloris/performance.csv"
    json_output_file = "/var/log/goloris/performance.json"

    # poor mans arg parse
    if len(sys.argv) > 1:
        input_file = sys.argv[1]

    if len(sys.argv) > 2:
        csv_output_file = sys.argv[2]

    if len(sys.argv) > 3:
        json_output_file = sys.argv[3]

    # get our samples
    metadata, samples = parse_samples(input_file)

    # prep the data
    aggregates = aggregate_samples(samples)

    # write it all out
    dump_samples_csv(samples, csv_output_file)
    dump_samples_json(metadata, aggregates, samples, json_output_file)