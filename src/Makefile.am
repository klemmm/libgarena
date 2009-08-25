INCLUDES=-I../include/

lib_LTLIBRARIES= \
	libgarena.la

libgarena_la_SOURCES= \
	garena.c gsp.c gcrp.c gp2pp.c util.c error.c

bin_PROGRAMS= client

client_LDADD= libgarena.la

client_SOURCES= \
	client.c
