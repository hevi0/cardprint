# Install
BIN = cardprint

# Flags
CFLAGS += -DCFLAG_APPNAME=\"$(BIN)\" -std=c99 -pedantic -O0
CFLAGS += `sdl2-config --cflags`

SRC = main.c
OBJ = $(SRC:.c=.o)

ifeq ($(OS),Windows_NT)
#TODO
#BIN := $(BIN).exe
LIBS = -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lm
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
#TODO		LIBS = -lSDL2 -framework OpenGL -lm
	else
		#LIBS += -lm -ldl `sdl2-config --libs` -lmingw64 -lSDLmain -lSDL
		LIBS += -lm -ldl -lmingw32 -lSDLmain -lSDL -lSDL2_image
	endif
endif

$(BIN):
	@mkdir -p build
	rm -f build/$(BIN) $(OBJS)
	$(CC) $(SRC) $(CFLAGS) -o build/$(BIN) $(LIBS)
