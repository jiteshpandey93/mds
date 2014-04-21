# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.  This file is offered as-is,
# without any warranty.


# The package path prefix, if you want to install to another root, set DESTDIR to that root.
PREFIX ?= /usr
# The command path excluding prefix.
BIN ?= /bin
# The library path excluding prefix.
LIB ?= /lib
# The executable library path excluding prefix.
LIBEXEC ?= /libexec
# The resource path excluding prefix.
DATA ?= /share
# The command path including prefix.
BINDIR ?= $(PREFIX)$(BIN)
# The library path including prefix.
LIBDIR ?= $(PREFIX)$(LIB)
# The executable library path including prefix.
LIBEXECDIR ?= $(PREFIX)$(LIBEXEC)
# The resource path including prefix.
DATADIR ?= $(PREFIX)$(DATA)
# The generic documentation path including prefix.
DOCDIR ?= $(DATADIR)/doc
# The info manual documentation path including prefix.
INFODIR ?= $(DATADIR)/info
# The license base path including prefix.
LICENSEDIR ?= $(DATADIR)/licenses

# The name of the package as it should be installed.
PKGNAME ?= blueshift


# Optimisation level (and debug flags.)
OPTIMISE = -Og -g

# Enabled Warnings.
WARN = -Wall -Wextra -pedantic -Wdouble-promotion -Wformat=2 -Winit-self       \
       -Wmissing-include-dirs -Wtrampolines -Wfloat-equal -Wshadow             \
       -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls           \
       -Wnested-externs -Winline -Wno-variadic-macros -Wsign-conversion        \
       -Wswitch-default -Wconversion -Wsync-nand -Wunsafe-loop-optimizations   \
       -Wcast-align -Wstrict-overflow -Wdeclaration-after-statement -Wundef    \
       -Wbad-function-cast -Wcast-qual -Wwrite-strings -Wlogical-op            \
       -Waggregate-return -Wstrict-prototypes -Wold-style-definition -Wpacked  \
       -Wvector-operation-performance -Wunsuffixed-float-constants             \
       -Wsuggest-attribute=const -Wsuggest-attribute=noreturn                  \
       -Wsuggest-attribute=pure -Wsuggest-attribute=format -Wnormalized=nfkc

# The C standard used in the code.
STD = gnu99

# Options for the C compiler.
C_FLAGS = $(OPTIMISE) $(WARN) -std=$(STD) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS)  \
          -ftree-vrp -fstrict-aliasing -fipa-pure-const -fstack-usage       \
          -fstrict-overflow -funsafe-loop-optimizations -fno-builtin        \
	  -D_GNU_SOURCE -pthread


# Build rules.

.PHONY: all
all: bin/mds bin/mds-server


bin/%: obj/%.o
	mkdir -p bin
	gcc $(C_FLAGS) -o $@ $^

obj/%.o: src/%.c src/%.h src/config.h
	mkdir -p obj
	gcc $(C_FLAGS) -c -o $@ $<



# Clean rules.

.PHONY: clean
clean:
	-rm -r obj bin

