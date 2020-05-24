#!/bin/bash

readonly DB_PATH=

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 port"
    exit 1
fi

pkill -SIGABRT postgres

cd ../aurora_logic_node
if [ $# -lt 2 ]; then
	echo "No configure of local node"
else
	echo "Configuration of local node"
	./configure --prefix=`pwd`/pgsql1 --enable-depend --enable-cassert > /dev/null
fi

echo "Start compilation of logic node"
if ! (make -j10 && make install) > /dev/null; then
    echo "Error building logic node"
    exit 1
fi

cd contrib/read_functions/
echo "Start logic extention compilation"
if ! (make install) > /dev/null; then
    echo "Error logic extention compilation"
    exit 1
fi
cd ../../

cd ../aurora_storage_node

if [ $# -lt 2 ]; then
	echo "No configure of storage node"
else
	echo "Configuration of storage node"
	./configure --prefix=`pwd`/pgsql1 --enable-depend --enable-cassert > /dev/null
fi

echo "Start compilation of storage node"
if ! (make -j10 && make install) > /dev/null; then
    echo "Error building storage node"
    exit 1
fi

cd contrib/read_functions/
echo "Start storage extention compilation"
if ! (make install) > /dev/null; then
    echo "Error storage extention compilation"
    exit 1
fi

cd ../../../

if [ -d LocalDB ]; then
    echo "Remove old Database"
    rm -rf LocalDB
fi
# todo remove this ???
echo "Database Initialisation:"
./aurora_logic_node/pgsql1/bin/initdb LocalDB/Logic_node

echo port=$1 >> LocalDB/Logic_node/postgresql.conf

cd aurora_logic_node/pgsql1/bin

echo "Start of Primary node:"
./pg_ctl -D../../../LocalDB/Logic_node -l ../../../LocalDB/pg_logic_logs start
./psql postgres -p $1 -c "CREATE EXTENSION read_functions;"

cd ../../../aurora_storage_node/pgsql1/bin

for node_number in {1..2}
do
	echo "Creating storage node_"$node_number" on port: $(($1 + $node_number))"
	#./initdb ~/LocalDB/Storage_node_$node_number

    ./pg_basebackup -p $1 -D ../../../LocalDB/Storage_node_$node_number -Fp -Xs -P -R
	echo "Change config"
	echo port=$(($1 + $node_number)) >> ../../../LocalDB/Storage_node_$node_number/postgresql.conf
 #   echo "shared_preload_libraries='read_functions'" >> ~/LocalDB/Storage_node_$node_number/postgresql.conf
	echo "Start storage node"
	./pg_ctl -D ../../../LocalDB/Storage_node_$node_number/ -l ../../../LocalDB/pg_storage_logs start

        echo "Start function with backend"
	./psql postgres -p $(($1 + $node_number)) -c "SELECT read_functions_mon_main(0);" &
  #  echo "load 'read_functions';" | ./psql postgres -p $(($1 + $node_number))
done


