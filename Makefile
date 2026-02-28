CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -Iinclude $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lm

SRC     = src/main.c src/ssd1305.c src/ssd1305_hal_sdl.c src/display.c \
          src/font5x7.c src/dashboard.c src/config_page.c src/telemetry_rx.c \
          src/dirt_rally_rx.c src/assetto_rx.c
OBJ     = $(SRC:.c=.o)
BIN     = racecontroller

# Telemetry simulator (standalone, no SDL dependency)
SIM_CC     = gcc
SIM_CFLAGS = -Wall -Wextra -std=c99 -Iinclude
SIM_SRC    = sim/telemetry_sim.c
SIM_BIN    = telemetry_sim

# Racepad bridge (standalone, no SDL dependency)
BRIDGE_CC     = gcc
BRIDGE_CFLAGS = -Wall -Wextra -std=c99 -Iinclude
BRIDGE_SRC    = src/racepad_bridge.c
BRIDGE_BIN    = racepad_bridge

PICO_CC       = gcc
PICO_CFLAGS   = -Wall -Wextra -std=c99 -Iinclude
PICO_SRC      = src/pico_scroll_bridge.c
PICO_BIN      = pico_scroll_bridge

all: $(BIN)

sim: $(SIM_BIN)
bridge: $(BRIDGE_BIN)
pico: $(PICO_BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(SIM_BIN): $(SIM_SRC)
	$(SIM_CC) $(SIM_CFLAGS) -o $@ $< -lm

$(BRIDGE_BIN): $(BRIDGE_SRC)
	$(BRIDGE_CC) $(BRIDGE_CFLAGS) -o $@ $< -lm

$(PICO_BIN): $(PICO_SRC)
	$(PICO_CC) $(PICO_CFLAGS) -o $@ $< -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN) $(SIM_BIN) $(BRIDGE_BIN) $(PICO_BIN)

.PHONY: all sim bridge pico clean
