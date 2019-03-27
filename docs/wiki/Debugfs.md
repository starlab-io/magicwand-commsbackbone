The debugfs is located at:

/sys/kernel/debug/mwcomms

and contains 3 files:

message_counts -> read only
    returns a formatted table of all MtRequest and response times as they are read/written from/to the ring buffer

reset -> write only
    when any value is written to this file, all the counts displayed in message_counts are reset

tracing_on -> read/write
    If the value is nonzero, tracing is enabled, and the counts per message type should increase as the are 
    written to/read from the ring buffer