#! /bin/bash
gcc main.c sqlite_srv_logger.c -lducq -lsqlite3 -llua -lm -o ducq_server.out -rdynamic
