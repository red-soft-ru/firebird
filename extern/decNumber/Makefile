LIBRARY=libdecFloat.a

$(LIBRARY):		$(wildcard *.c) $(wildcard *.h) Makefile
	$(RM) -f *.o
	$(CC) -c -O3 -fPIC $(subst decCommon.c,,$(subst decBasic.c,,$(wildcard *.c)))
	$(AR) crs $(LIBRARY) *.o
	$(RM) -f *.o

.PHONY:	clean
clean:
	$(RM) -f *.o $(LIBRARY)
