import json,random

idcount=0

def main():
	test_spec=generate_test_spec()
	with open("sample_test_spec.json","w") as fileobj:
		fileobj.write(json.dumps(test_spec, sort_keys=True, indent=2))

def generate_test_spec():
	global idcount
	test_spec={
		"id":0,
		"comment":"This is a test that does some stuf and does some other stuff",
		"tags":["http","wbradmoore","bad_network","slowloris"],
		"server":None,
		"clients":[],
		"monitors":[],
		"schedule":create_random_schedule(),
		}

	test_spec["server"]={
		"comment":"a server on a crappy connection",
		"services":["http","nodejs_tcp_stream"],
		"latency_ms_mean":random.randrange(10,1000),
		"bandwidth_kbits_mean":random.randrange(1000,100000),
		"dropped_packet_probability":random.randrange(0,1000)/1000.,
		"duplicated_packet_probability":random.randrange(0,1000)/1000.,
		"corrupted_packet_probability":random.randrange(0,10000)/10000.}
	test_spec["server"]\
		["bandwidth_kbits_stdev"]=random.randrange(test_spec["server"]["bandwidth_kbits_mean"],test_spec["server"]["bandwidth_kbits_mean"]*10)/30
	test_spec["server"]\
		["latency_ms_stdev"]=random.randrange(test_spec["server"]["latency_ms_mean"],test_spec["server"]["latency_ms_mean"]*10)/30


	for x in xrange(0,10):
		test_spec["clients"].append(create_random_client())

	attacker={
		"id":idcount,
		"comment":"THIS IS AN ATTACKER that starts out as an http browser",
		"type":"http",
		"docker_image":"single-session-browser-becomes-slowloris",
		"options":["attackstart=60"],
		"latency_ms_mean":random.randrange(10,1000),
		"bandwidth_kbits_mean":random.randrange(1000,100000),
		"dropped_packet_probability":random.randrange(0,1000)/1000.,
		"duplicated_packet_probability":random.randrange(0,1000)/1000.,
		"corrupted_packet_probability":random.randrange(0,10000)/10000.
		};idcount+=1;
	attacker\
		["bandwidth_kbits_stdev"]=random.randrange(test_spec["server"]["bandwidth_kbits_mean"],test_spec["server"]["bandwidth_kbits_mean"]*10)/30
	attacker\
		["latency_ms_stdev"]=random.randrange(test_spec["server"]["latency_ms_mean"],test_spec["server"]["latency_ms_mean"]*10)/30

	test_spec["clients"].append(attacker)

	test_spec["monitors"].append({
		"id":0,
		"comment":"this is the thing that looks at timings of tcp responses based on seq/ack numbers...protocol agnostic",
		"name":"tcp_timing",
		"is_docker":False,
		"options":["measurement_frequency_ms=1000"],
		"monitored_client_ids":[0,1,2],
		})

	return test_spec

def create_random_client():
	global idcount
	client={}
	client["id"]=idcount
	idcount+=1
	client["comment"]="this client is meant to emulate a 12 year old browsing minecraft forums on a shoddy wifi connection to a robustly connected AP, or something"
	client["type"]="http"
	client["docker_image"]="multi-session-browser"
	client["options"]=[]
	client["latency_ms_mean"]=random.randrange(10,1000)
	client["latency_ms_stdev"]=random.randrange(client["latency_ms_mean"],client["latency_ms_mean"]*10)/30
	client["bandwidth_kbits_mean"]=random.randrange(1000,100000)
	client["bandwidth_kbits_stdev"]=random.randrange(client["bandwidth_kbits_mean"],client["bandwidth_kbits_mean"]*10)/30
	client["dropped_packet_probability"]=random.randrange(0,1000)/1000
	client["duplicated_packet_probability"]=random.randrange(0,1000)/1000
	client["corrupted_packet_probability"]=random.randrange(0,10000)/10000
	return client

def create_random_schedule():
	schedule={}
	schedule["comment"]="this is a short test with a certain schedule of events that repesents an attack during a busy ticketmaster sale"
	schedule["events"]=[]
	schedule["events"].append({
		"time":0,
		"comment":"apache ho!",
		"component":"server",
		"action":"start",
		"ids":[],
		})
	schedule["events"].append({
		"time":2,
		"comment":"this is to launch monitors",
		"component":"monitors",
		"action":"start",
		"ids":["all"],
		})
	schedule["events"].append({
		"time":10,
		"comment":"this is to launch clients 0, 1, and 2",
		"component":"clients",
		"action":"start",
		"ids":[0,1,2],
		})
	schedule["events"].append({
		"time":20,
		"comment":"this is to launch slowloris",
		"component":"slowloris",
		"action":"start",
		"ids":[],
		})
	schedule["events"].append({
		"time":30,
		"comment":"this is to launch the rest of the clients",
		"component":"clients",
		"action":"start",
		"ids":["all"],
		})
	schedule["events"].append({
		"time":120,
		"comment":"this is to tear down",
		"component":"all",
		"action":"stop",
		"ids":[],
		})
	return schedule

if __name__ == "__main__":
	main()