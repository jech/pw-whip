LIBDATACHANNEL_ROOT=$(HOME)/src/libdatachannel
PREFIX=/usr/local
MANDIR=$(PREFIX)/share/man

CPPFLAGS = -I $(LIBDATACHANNEL_ROOT)/include `pkg-config --cflags libpipewire-0.3` `pkg-config --cflags opus`
CFLAGS = -Wall -O
CXXFLAGS = -Wall -O
LDLIBS = -ldatachannel -lcurl `pkg-config --libs libpipewire-0.3` `pkg-config --libs opus`
OBJS = main.o whip.o pipewire.o

pw-whip: $(OBJS)
	$(CXX) -o $@ $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS)

.PHONY: install
install: pw-whip
	-rm -f $(TARGET)$(PREFIX)/bin/pw-whip
	mkdir -p $(TARGET)$(PREFIX)/bin
	cp -f pw-whip $(TARGET)$(PREFIX)/bin/
	mkdir -p $(TARGET)$(MANDIR)/man1
	cp -f pw-whip.man $(TARGET)$(MANDIR)/man1/pw-whip.1

.PHONY: uninstall
uninstall:
	-rm -f $(TARGET)$(PREFIX)/bin/pw-whip
	-rm -f $(TARGET)$(MANDIR)/man1/pw-whip.1

.PHONY: clean
clean:
	rm -f pw-whip $(OBJS) *~
