#!/bin/bash

if [ -n "$1" ]
then
	cd "$1"
else
	cd /opt/pgsql/
fi

cd bin/

for node_number in {1..3}
do
	echo "Stoping storage node_"$node_number" on port: 543$((2 + $node_number))"
	./pg_ctl -D ~/LocalDB/Storage_node_$node_number/ stop
done

echo "Stopping of Primary node:"
./pg_ctl -D ~/LocalDB/Logic_node stop
