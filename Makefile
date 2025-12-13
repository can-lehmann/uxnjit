CFLAGS := $(shell llvm-config --cflags --libs)

HEADER_FILES := \
	$(wildcard modules/metajit.cpp/*.hpp) \
	modules/metajit.cpp/jitir.hpp \
	modules/metajit.cpp/jitir_llvmapi.hpp

main: main.cpp ${HEADER_FILES}
	clang++ -o $@ $< ${CFLAGS}

modules/metajit.cpp/jitir.hpp: modules/metajit.cpp/jitir.py modules/metajit.cpp/jitir.tmpl.hpp
	make -C modules/metajit.cpp jitir.hpp

modules/metajit.cpp/jitir_llvmapi.hpp: modules/metajit.cpp/jitir.py modules/metajit.cpp/jitir_llvmapi.tmpl.hpp
	make -C modules/metajit.cpp jitir_llvmapi.hpp
