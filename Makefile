CFLAGS := $(shell llvm-config --cflags --libs)

HEADER_FILES := \
	$(wildcard modules/metajit.cpp/*.hpp) \
	modules/metajit.cpp/jitir.hpp \
	modules/metajit.cpp/jitir_llvmapi.hpp

all: bin/main bin/uxn.ll

run: bin/main bin/uxn.ll
	$^

bin/main: src/main.cpp ${HEADER_FILES}
	clang++ -o $@ $< ${CFLAGS}

modules/metajit.cpp/jitir.hpp: modules/metajit.cpp/jitir.py modules/metajit.cpp/jitir.tmpl.hpp
	make -C modules/metajit.cpp jitir.hpp

modules/metajit.cpp/jitir_llvmapi.hpp: modules/metajit.cpp/jitir.py modules/metajit.cpp/jitir_llvmapi.tmpl.hpp
	make -C modules/metajit.cpp jitir_llvmapi.hpp

bin/uxn.ll: src/uxn.c src/uxn.h
	clang -O2 -S -emit-llvm -o $@ $<
	opt -S --passes=lower-switch $@ -o $@

clean:
	-rm -rf bin/*
	mkdir bin
