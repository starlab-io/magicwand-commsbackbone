import json,argparse,threading

test_specification={}
active_nodes={'server':[],'clients':[],'monitors':[]}

def get_args():
	parser = argparse.ArgumentParser()
	parser.add_argument('-t', '--testspecfile', type=str, required=True)
	# parser.add_argument('-i', '--imagedir', type=str, required=True)
	parser.add_argument('-o', '--outputdir', type=str, required=True)
	args=parser.parse_args()
	return args

def main(args):
	global test_specification
	with open(args.testspecfile) as test_specification_file:    
		test_specification = json.load(test_specification_file)
	schedule_events()
	# print test_specification['schedule']

def schedule_events():
	global test_specification
	events_by_time=get_events_by_time(test_specification['schedule']['events'])
	for event_time in events_by_time.iterkeys():
		for event in events_by_time[event_time]:
			threading.Timer(event_time,run_event,args=[event]).start()
			


def get_events_by_time(events):
	event_times=[]
	events_by_time={}
	for event in events:
		if event['time'] not in events_by_time.keys():
			events_by_time[event['time']]=[]
		events_by_time[event['time']].append(event)
	return events_by_time

def run_event(event):
	global test_specification,active_nodes
	if event['action']=='start':
		if event['component']=='server':
			if 0 in active_nodes['server']:
				print "Attempted to start started server";exit(1)
			print "Launching Server:"
			print(json.dumps(test_specification['server'], sort_keys=True, indent=4))
			active_nodes['server'].append(0)
		elif event['component']=='client':
			print "Launching Clients:"
			for client in filter(lambda x: (x['id'] in event['ids'] or u'all' in event['ids']) and x['id'] not in active_nodes['clients'], test_specification['clients']):
				print(json.dumps(client, sort_keys=True, indent=4))
				active_nodes['clients'].append(client['id'])
		elif event['component']=='monitor':
			print "Launching Monitors:"
			for monitor in filter(lambda x: (x['id'] in event['ids'] or u'all' in event['ids']) and x['id'] not in active_nodes['monitors'], test_specification['monitors']):
				print(json.dumps(monitor, sort_keys=True, indent=4))
				active_nodes['monitors'].append(monitor['id'])
		elif event['component']=='all':
			print "Component \"all\" currently only for stopping";exit(1)
		else:
			print "unknown component "+event['component'];exit(1)
	if event['action']=='update':
		if event['component']=='server':
			if 0 not in active_nodes['server']:
				print "Attempted to update stopped server";exit(1)
			print "Updating Server:"
			print(json.dumps(test_specification['server'], sort_keys=True, indent=4))
		elif event['component']=='client':
			print "Updating Clients:"
			for client in filter(lambda x: (x['id'] in event['ids'] or u'all' in event['ids']) and x['id'] in active_nodes['clients'], test_specification['clients']):
				print(json.dumps(client, sort_keys=True, indent=4))
		elif event['component']=='monitor':
			print "Updating Monitors:"
			for monitor in filter(lambda x: (x['id'] in event['ids'] or u'all' in event['ids']) and x['id'] in active_nodes['monitors'], test_specification['monitors']):
				print(json.dumps(monitor, sort_keys=True, indent=4))
		elif event['component']=='all':
			print "Component \"all\" currently only for stopping";exit(1)
		else:
			print "unknown component "+event['component'];exit(1)
	if event['action']=='stop':
		if event['component']=='server':
			if 0 not in active_nodes['server']:
				print "Attempted to stop stopped server";exit(1)
			print "Killing Server:"
			print(json.dumps(test_specification['server'], sort_keys=True, indent=4))
		elif event['component']=='client':
			print "Killing Clients:"
			for client in filter(lambda x: (x['id'] in event['ids'] or u'all' in event['ids']) and x['id'] in active_nodes['clients'], test_specification['clients']):
				print(json.dumps(client, sort_keys=True, indent=4))
				active_nodes['clients'].remove(client['id'])
		elif event['component']=='monitor':
			print "Killing Monitors:"
			for monitor in filter(lambda x: (x['id'] in event['ids'] or u'all' in event['ids']) and x['id'] in active_nodes['monitors'], test_specification['monitors']):
				print(json.dumps(monitor, sort_keys=True, indent=4))
				active_nodes['monitors'].remove(monitor['id'])
		elif event['component']=='all':
			print "Shutting Down all."
		else:
			print "unknown component "+event['component'];exit(1)

if __name__ == '__main__':
	args=get_args()
	main(args)