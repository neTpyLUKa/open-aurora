#!/bin/bash

if [ -n "$1" ]
then
	cd "$1"
else
	cd /opt/pgsql/
fi

cd bin/

if [ -d ~/LocalDB ]; then
	echo "Remove old Database"
	rm -rf ~/LocalDB	
fi



echo "Database Initialisation:"
./initdb ~/LocalDB/Logic_node

echo "Start of Primary node:"
./pg_ctl -D ~/LocalDB/Logic_node start

for node_number in {1..3}
do
	echo "Creating storage node_"$node_number" on port: 543$((2 + $node_number))"
	./pg_basebackup -p 5432 -D ~/LocalDB/Storage_node_$node_number -Fp -Xs -P -R
	echo "Change config"
	echo port=543$((2 + $node_number)) >> ~/LocalDB/Storage_node_$node_number/postgresql.conf
	echo "Start storage node"
	./pg_ctl -D ~/LocalDB/Storage_node_$node_number/ start
done
