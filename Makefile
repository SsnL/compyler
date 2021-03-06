# Top-level Makefile for Project 2.
# 
# Targets:
#   gmake [ default ]        Compiles sources in src directory
#   gmake check              Compiles sources and runs tests.  The parameter
#                            APYC indicates which compiler to run.
#   gmake clean              Removes unnecessary files, including those that
#                            are reproducible.
#   gmake depend	     Compute makefile dependencies and store
#                            in .depend.  Use this whenever you add a new
#                            source file or change an #include line.
#   gmake really-clean       Same as 'clean', also removes the file .depend.
#

APYC = $(shell echo `pwd`/apyc)

.PHONY: default compile check check1 check2 clean really-clean depend

default: compile

compile:
	$(MAKE) -C src compile

check:
	$(MAKE) -C tests APYC=$(APYC) check

check1:
	$(MAKE) -C tests APYC=$(APYC) check1

check2:
	$(MAKE) -C tests APYC=$(APYC) check2

check3:
	$(MAKE) -C tests APYC=$(APYC) check3

clean:
	$(RM) *~ apyc
	$(MAKE) -C src clean
	$(MAKE) -C tests clean

really-clean: clean
	$(MAKE) -C src really-clean
	$(MAKE) -C tests really-clean


depend:
	$(MAKE) -C src depend

