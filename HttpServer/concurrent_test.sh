#!/bin/bash

printf "GET /foo.txt HTTP/1.1\r\n\r\n" | ./cse130_nc localhost 3000 &
printf "GET /foo.txt HTTP/1.1\r\n\r\n" | ./cse130_nc localhost 3000 &
printf "GET /foo.txt HTTP/1.1\r\n\r\n" | ./cse130_nc localhost 3000 &

wait