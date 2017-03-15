#!/bin/bash
gcc gmems.c -o gmems -lpthread -lrt `pkg-config --cflags --libs gtk+-2.0 --libs gthread-2.0`
