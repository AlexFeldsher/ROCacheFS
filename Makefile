CC=g++
CFLAGS=-std=c++11 -DNDEBUG
OBJECTS=CacheFS.o Block.o
FILES=Makefile README CacheFS.cpp Block.h Block.cpp Answers.pdf
LIB=CacheFS.a
AR=ar
ARFLAGS=rcs

lib: $(OBJECTS)
	$(AR) $(ARFLAGS) $(LIB) $(OBJECTS)
	rm -f $(OBJECTS)
Block.o: Block.h Block.cpp debug.h
	$(CC) $(CFLAGS) -c Block.cpp
CacheFS.o: CacheFS.h CacheFS.h debug.h
	$(CC) $(CFLAGS) -c CacheFS.cpp
tar: $(FILES)
	tar -cvf ex4.tar $(FILES)
clean:
	rm -f $(OBJECTS) $(LIB)
.PHONE: clean lib tar
