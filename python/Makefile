-include ../Makefile.config
include ../Makefile.include

all:
	python3 setup.py build_ext --inplace

clean:
	rm -rf build/
	rm -f cpddl.c
	rm -f *.so
