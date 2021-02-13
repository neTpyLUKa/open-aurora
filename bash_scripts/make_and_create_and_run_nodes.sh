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

#exit 0 # tmp

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

#cp -r LocalDB/Logic_node LocalDB/Storage_node_1
	
echo port=$1 >> LocalDB/Logic_node/postgresql.conf
#cp logic_node.auto.conf LocalDB/Logic_node/postgresql.auto.conf

#./aurora_logic_node/pgsql1/bin/psql postgres -p 5432 -c "CREATE EXTENSION read_functions;"

aurora_logic_node/pgsql1/bin/pg_ctl -D LocalDB/Logic_node -l LocalDB/pg_logic_logs start

./aurora_storage_node/pgsql1/bin/pg_basebackup -p $1 -D LocalDB/Storage_node_1 -Xf -R


echo port=5433 >> LocalDB/Storage_node_1/postgresql.conf

#./aurora_storage_node/pgsql1/bin/pg_basebackup -p $1 -D LocalDB/Storage_node_1 -R


#cp storage_node.auto.conf LocalDB/Storage_node_1/postgresql.auto.conf
 	
./aurora_storage_node/pgsql1/bin/pg_ctl -D LocalDB/Storage_node_1/ -l LocalDB/pg_storage_logs start
   	
#./aurora_storage_node/pgsql1/bin/psql postgres -p 5433 -c "SELECT read_functions_mon_main(0);" &


#echo "Start of Primary node:"
#cd ../../../aurora_storage_node/pgsql1/bin

#for node_number in {1..1}
#do
#	echo "Creating storage node_"$node_number" on port: $(($1 + $node_number))"
	#./initdb ~/LocalDB/Storage_node_$node_number

#echo "Change config"
 #   echo "shared_preload_libraries='read_functions'" >> ~/LocalDB/Storage_node_$node_number/postgresql.conf
#	echo "Start storage node"
 #       echo "Start function with backend"
  #  echo "load 'read_functions';" | ./psql postgres -p $(($1 + $node_number))
#done
