#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 port"
    exit 1
fi

pkill -SIGABRT postgres

cd ../aurora_logic_node
if ! (make -j10 && make install) > /dev/null; then
    echo "Error building logic node"
    exit 1
fi

cd ../aurora_storage_node
if ! (make -j10 && make install) > /dev/null; then
    echo "Error building storage node"
    exit 1
fi

cd ../aurora_logic_node/pgsql1/bin

if [ -d ~/LocalDB ]; then
    echo "Remove old Database"
    rm -rf ~/LocalDB
fi

echo "Database Initialisation:"
./initdb ~/LocalDB/Logic_node > /dev/null

echo port=$1 >> ~/LocalDB/Logic_node/postgresql.conf

echo "Start of Primary node:"
./pg_ctl -D ~/LocalDB/Logic_node start > /dev/null

cd ../../../aurora_storage_node/pgsql1/bin

for node_number in {1..2}
do
	echo "Creating storage node_"$node_number" on port: $(($1 + $node_number))"
	./pg_basebackup -p $1 -D ~/LocalDB/Storage_node_$node_number -Fp -Xs -P -R > /dev/null
	echo "Change config"
	echo port=$(($1 + $node_number)) >> ~/LocalDB/Storage_node_$node_number/postgresql.conf
	echo "Start storage node"
	./pg_ctl -D ~/LocalDB/Storage_node_$node_number/ start > /dev/null
done
