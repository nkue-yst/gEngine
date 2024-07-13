#!/bin/sh

cmake -S Build/ -B Binaries/ -G "Unix Makefiles"
cd Binaries/ && make
