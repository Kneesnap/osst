# Makefile for the osst (OnStream SC-30/50/70 ADR tape) driver
KDIR := /usr/src/linux

TARGETS := Rules.make arch osst.o
.PHONY: osst.o

default: $(TARGETS)


Rules.make: $(KDIR)/Rules.make
	cp -pf $(KDIR)/Rules.make .

arch: $(KDIR)/arch
	rm -f arch
	ln -s $(KDIR)/arch .
	
clean:
	rm -f $(TARGETS)
	
osst.o: 
	$(MAKE) -f Makefile.osst KDIR=$(KDIR)

install: 
	$(MAKE) -f Makefile.osst KDIR=$(KDIR) install_mod
	
load:
	$(MAKE) -f Makefile.osst KDIR=$(KDIR) load_module
	
