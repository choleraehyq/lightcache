.PHONY : all
all: demo 

demo: demo.cc lightcache.h
	g++ demo.cc -g -Wall -std=c++11 -o demo  

.PHONY : clean
clean: 
	-rm demo
	-rm *~
	-rm -f demo.out
