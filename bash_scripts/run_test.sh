#!/bin/bash

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 port"
    exit 1
fi

cd ../aurora_logic_node/pgsql1/bin/

echo "Turn on remote storage"

echo "
alter system set enable_remote_storage to on;
select pg_reload_conf();
" | ./psql postgres -p $1

echo "Init and run test"

./pgbench -i -n -p 6000 postgres && ./pgbench -S -n -l -p 6000 postgres