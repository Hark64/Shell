CFLAGS = -Wall -Werror -pedantic -g

mush2: mush2.o
  gcc $(CFLAGS) libmush.a -o mush2 mush2.o
mush2.o: mush2.c
  gcc $(CFLAGS) -c mush2.c                
