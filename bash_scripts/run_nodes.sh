#!/bin/bash

if [ -n "$1" ]
then
	cd "$1"
else
	cd /opt/pgsql/
fi

cd bin/

if [ ! -d ~/LocalDB ]; then
	echo "No Database!"
else 
	echo "Start of Primary node:"
	./pg_ctl -D ~/LocalDB/Logic_node start

	for node_number in {1..3}
	do
		echo "Start storage node_storage node_"$node_number" on port: 543$((2 + $node_number))"
		./pg_ctl -D ~/LocalDB/Storage_node_$node_number/ start
	done

fi



