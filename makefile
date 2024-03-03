objects = Server.o Httpd.o

Server : $(objects)
	g++ -o Server $(objects)

Server.o : Httpd.h
Httpd.o : Httpd.h

.PHONY : clean
clean :
	rm Server $(objects)

