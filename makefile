CC = gcc

CFLAGS = -I ./include
LDFLAGS =  -lpthread -lm -ldl

CSRCS = scpulse.c
BIN = scpulse

HTML_NAME = scpulse_web.html

LIBS_DIR = lib
SLIBS_LINUX = $(LIBS_DIR)/raylib_linux.a
SLIBS_WEB = $(LIBS_DIR)/raylib_web.a

SHELL := /bin/bash

all: linux web

linux: $(CSRCS)
	$(CC) $(CFLAGS) -o $(BIN) $(CSRCS) $(SLIBS_LINUX) $(LDFLAGS)

web: $(CSRCS)
	source "../emsdk/emsdk_env.sh"; emcc -o $(HTML_NAME) scpulse.c -Os -Wall $(SLIBS_WEB)  -I . -I include/ -L . -L lib/ -s USE_GLFW=3 -s ASYNCIFY --preload-file resources/ -s TOTAL_STACK=64MB -s INITIAL_MEMORY=128MB -sALLOW_MEMORY_GROWTH -s ASSERTIONS -sGL_ENABLE_GET_PROC_ADDRESS -DPLATFORM_WEB \

clean:
	rm $(BIN)
