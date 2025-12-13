CFLAGS := $(shell llvm-config --cflags --libs)

main: main.cpp modules/metajit.cpp/jitir.hpp modules/metajit.cpp/jitir_llvmapi.hpp
	clang++ -o $@ $< ${CFLAGS}

modules/metajit.cpp/jitir.hpp: modules/metajit.cpp/jitir.py modules/metajit.cpp/jitir.tmpl.hpp
	make -C modules/metajit.cpp jitir.hpp

modules/metajit.cpp/jitir_llvmapi.hpp: modules/metajit.cpp/jitir.py modules/metajit.cpp/jitir_llvmapi.tmpl.hpp
	make -C modules/metajit.cpp jitir_llvmapi.hpp
