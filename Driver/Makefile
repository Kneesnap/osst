# Makefile for the osst (OnStream SC-30/50/70 ADR tape) driver
# (w) 3/2000, garloff@suse.de

KDIR := /usr/src/linux

TARGET := osst.o
TARGETS := Rules.make arch $(TARGET)
.PHONY: osst.o

default: $(TARGETS)


Rules.make: $(KDIR)/Rules.make
	cp -pf $(KDIR)/Rules.make .

arch: $(KDIR)/arch
	rm -f arch
	ln -s $(KDIR)/arch .
	
clean:
	rm -f $(TARGETS) .$(TARGET).flags
	
$(TARGET): 
	$(MAKE) -f Makefile.osst KDIR=$(KDIR)

install: $(TARGETS)
	$(MAKE) -f Makefile.osst KDIR=$(KDIR) install_mod
	
load: $(TARGETS)
	$(MAKE) -f Makefile.osst KDIR=$(KDIR) load_module
	
