#!/usr/bin/python

MW_XENSTORE_ROOT = b"/mw"

import pyxs

with pyxs.Client() as c:
    m = c.monitor()
    m.watch( MW_XENSTORE_ROOT, b"MW INS watcher" )

    events = m.wait()
    for e in events:
        path = e[0]
        value = None
        if ( c.exists( path ) ):
            value = c[path]

        assert path.startswith( MW_XENSTORE_ROOT ), "Unexpected path {0}".format(path)

        if ('ip_addrs' in path):
            break
