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
	@echo "No web builds yet..."

all: linux web

clean:
	rm $(BIN)
