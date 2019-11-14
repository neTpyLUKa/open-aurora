#!/bin/bash


pkill -SIGABRT postgres


if [ ! -d ~/LocalDB ]; then
	echo "No Database!"
	exit 1
fi

cd ../aurora_logic_node/pgsql1/bin

echo "Start Primary node:"
./pg_ctl -D ~/LocalDB/Logic_node start


cd ../../../aurora_storage_node/pgsql1/bin

for node_number in {1..1}
do
	echo "Start storage node_storage node_"$node_number""
	./pg_ctl -D ~/LocalDB/Storage_node_$node_number/ start
done




