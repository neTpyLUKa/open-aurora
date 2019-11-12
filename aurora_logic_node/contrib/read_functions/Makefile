MODULE_big = read_functions
OBJS = read_functions.o #blinsert.o blscan.o blutils.o blvacuum.o blvalidate.o $(WIN32RES)

EXTENSION = read_functions
DATA = read_functions--1.0.sql
PGFILEDESC = "read_functions access method - signature file based index"

MODULES = read_functions
subdir = contrib/read_functions
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk

