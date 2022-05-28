CFLAGS=-W -Wall -Wextra -O2 $(shell gfxprim-config --cflags)
LDLIBS=$(shell gfxprim-config --libs-widgets) -lgfxprim -lircclient
BIN=gpirc
DEP=$(BIN:=.dep)

all: $(DEP) $(BIN)

%.dep: %.c
	$(CC) $(CFLAGS) -M $< -o $@

-include $(DEP)

install:
	install -m 644 -D layout.json $(DESTDIR)/etc/gp_apps/$(BIN)/layout.json
	install -D $(BIN) -t $(DESTDIR)/usr/bin/

clean:
	rm -f $(BIN) *.dep *.o
