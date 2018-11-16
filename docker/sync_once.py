from mwstorage import Storage
from mwstorage.sync import sync_once

datafile_path = "/code/archive/netflow.json"
sync_once([datafile_path])
