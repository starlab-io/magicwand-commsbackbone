
The test harness orchestrates Apache, GoLoris, and a set of monitoring streams to generate performance and trace data for use
in building the models backing MagicWand, and verifying the models, and testing mitigations. These directions assume you are
in a working Docker environment.

Once the test harness has been run, the generated performance, trace, and log data can be processed, archived, and visualized.

Sample of the visualization:

![Test Performance Graphs](https://cloud.githubusercontent.com/assets/487102/16698487/784bdaa4-451c-11e6-8122-f56ce19a1af9.png)

# Running the Test Harness

```bash
  🚀  export TEST_DURATION=20
  🚀  ./monitor_test.sh 
Preparing...
  ! running performance collection for 20 seconds
Spinning up test instances
Creating network "testharness_default" with the default driver
Creating testharness_apache_1
Creating testharness_goloris_1
Creating testharness_httperf_1
Monitoring test (sync interval 2 seconds)...
  > running
  > running
  > running
  > running
  > running
  > running
  > running
  > running
  > running
  > running
  > running
  > running
  > running
  > running
  + collating HTTPerf results
  ! waiting for PIN & vmstat to complete tracing (this could take awhile)
  > running
  > running
  > running
  > running
  > running
  > running
  + collating ApachePerf results
  + collating GoLoris results
  ! Tearing down test harness containers
Removing testharness_httperf_1 ... done
Removing testharness_goloris_1 ... done
Removing testharness_apache_1 ... done
Removing network testharness_default
  . done
!! If you want to save these test results don't forget to run ./archive_test
!! Archived test results can be viewed using ./view_tests.sh
  🚀  ./archive_test 
Creating archive as [Patricks-MacBook-Pro.local-2016.07.08T14.59.06]

New test data.
    # Created performance test archive [Patricks-MacBook-Pro.local-2016.07.08T14.59.06]
    # Lines beginning with # will not be included in the archive message.

Updated history labeled [Patricks-MacBook-Pro.local-2016.07.08T14.59.06]
  🚀  ./view_tests.sh 
Point your browser to http://localhost:8080 for performance data
~/projects/magicwand/test-harness/display ~/projects/magicwand/test-harness
Serving HTTP on 0.0.0.0 port 8080 ...
```
