
# Running

## Verification Run

Configuration and connectivity between containers can be verified by running
`docker-compose` and running only the _Apache_ and _httperf_ containers:

```sh
docker-compose up apache httperf
```

HTTPerf will run for 120 seconds, with a rate of 100 requests per second.

## Evaluation Run

During an evaluation run, it is suggested that the containers be run in
detached mode. 

```sh
docker-compose up
```

Post run you can retrieve the *HTTPerf* performance logs
with `docker-compose`

```sh
docker-compose logs httperf
```

# Sample Output

```sh
httperf_1  | Starting HTTPERF
httperf_1  |      server: apache
httperf_1  |        port: 80
httperf_1  |       conns: 12000
httperf_1  |        rate: 100
httperf_1  |     timeout: 5
httperf_1  |     options: -v
httperf_1  | httperf --verbose --timeout=5 --client=0/1 --server=apache --port=80 --uri=/ --rate=100 --send-buffer=4096 --recv-buffer=16384 --num-conns=12000 --num-calls=1
httperf_1  | httperf: maximum number of open descriptors = 1048576
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.2   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 99.8    
httperf_1  | reply-rate = 100.2   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | reply-rate = 100.0   
httperf_1  | Maximum connect burst length: 1
httperf_1  | 
httperf_1  | Total: connections 12000 requests 12000 replies 12000 test-duration 119.990 s
httperf_1  | 
httperf_1  | Connection rate: 100.0 conn/s (10.0 ms/conn, <=2 concurrent connections)
httperf_1  | Connection time [ms]: min 0.2 avg 0.3 max 10.3 median 0.5 stddev 0.4
httperf_1  | Connection time [ms]: connect 0.0
httperf_1  | Connection length [replies/conn]: 1.000
httperf_1  | 
httperf_1  | Request rate: 100.0 req/s (10.0 ms/req)
httperf_1  | Request size [B]: 59.0
httperf_1  | 
httperf_1  | Reply rate [replies/s]: min 99.8 avg 100.0 max 100.2 stddev 0.1 (23 samples)
httperf_1  | Reply time [ms]: response 0.2 transfer 0.0
httperf_1  | Reply size [B]: header 228.0 content 44.0 footer 0.0 (total 272.0)
httperf_1  | Reply status: 1xx=0 2xx=12000 3xx=0 4xx=0 5xx=0
httperf_1  | 
httperf_1  | CPU time [s]: user 40.35 system 76.10 (user 33.6% system 63.4% total 97.0%)
httperf_1  | Net I/O: 32.3 KB/s (0.3*10^6 bps)
httperf_1  | 
httperf_1  | Errors: total 0 client-timo 0 socket-timo 0 connrefused 0 connreset 0
httperf_1  | Errors: fd-unavail 0 addrunavail 0 ftab-full 0 other 0
testharness_httperf_1 exited with code 0
```


# Rate Sampling

HTTPerf samples the reply rate every 5 seconds, with the reply rates printed
out at the end of the test run. Output lines from HTTPerf prefixed with `reply-rate` can
be used to build a performance histogram for the test run.

