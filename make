#!/bin/bash
echo GCC GCC GCC GCC
echo
time gcc -g -Wall -o $1 -levent $1.c -I /usr/local/include/ -L /usr/local/lib/
echo
echo clang clang clang clang
echo
time ~/llvm/Debug/bin/clang -g -Wall -o $1.clang -levent $1.c -I /usr/local/include/ -L /usr/local/lib/
echo
