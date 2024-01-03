CC = gcc

CFLAGS = -I ./include
LDFLAGS =  -lpthread -lm -ldl

CSRCS = scpulse.c
BIN = scpulse

LIBS_DIR = lib
SLIBS_LINUX = $(LIBS_DIR)/raylib_linux.a
SLIBS_WEB = $(LIBS_DIR)/raylib_web.a

linux: $(CSRCS)
	$(CC) $(CFLAGS) -o $(BIN) $(CSRCS) $(SLIBS_LINUX) $(LDFLAGS)

web: $(CSRCS)
	#source "/home/styxa/devel/emsdk/emsdk_env.sh"
	emcc -o foo.html scpulse.c -Os -Wall lib/raylib_web.a  -I . -I include/ -L . -L lib/ -s USE_GLFW=3 -s ASYNCIFY --preload-file resources/ -s TOTAL_STACK=64MB -s INITIAL_MEMORY=128MB -sALLOW_MEMORY_GROWTH -s ASSERTIONS -sGL_ENABLE_GET_PROC_ADDRESS -DPLATFORM_WEB

all: linux web

clean:
	rm $(BIN)
