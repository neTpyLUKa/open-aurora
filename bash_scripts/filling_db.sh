#!/bin/bash

cd  ../aurora_logic_node/pgsql1/bin

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 port"
    exit 1
fi

echo "Filling DB"
echo "
CREATE TABLE link (
   ID serial PRIMARY KEY,
   url VARCHAR (255) NOT NULL,
   name VARCHAR (255) NOT NULL,
   description VARCHAR (255),
   rel VARCHAR (50)
); 
insert into link values(1337, 1337, 1337, 1337, 1337);
" | ./psql postgres -p $1

echo "Restarting DB"
./pg_ctl -D ~/LocalDB/Logic_node -l ~/LocalDB/pg_storage_logs stop
./pg_ctl -D ~/LocalDB/Logic_node -l ~/LocalDB/pg_storage_logs start

