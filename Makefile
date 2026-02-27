CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -Iinclude $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lm

SRC     = src/main.c src/ssd1305.c src/ssd1305_hal_sdl.c src/display.c \
          src/font5x7.c src/dashboard.c src/config_page.c src/telemetry_rx.c \
          src/dirt_rally_rx.c
OBJ     = $(SRC:.c=.o)
BIN     = racecontroller

# Telemetry simulator (standalone, no SDL dependency)
SIM_CC     = gcc
SIM_CFLAGS = -Wall -Wextra -std=c99 -Iinclude
SIM_SRC    = sim/telemetry_sim.c
SIM_BIN    = telemetry_sim

all: $(BIN)

sim: $(SIM_BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(SIM_BIN): $(SIM_SRC)
	$(SIM_CC) $(SIM_CFLAGS) -o $@ $< -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN) $(SIM_BIN)

.PHONY: all sim clean
