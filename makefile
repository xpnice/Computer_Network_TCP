.PHONY:all
all:service client
service:server.o
	gcc -o server server.o
client:client.o 
	gcc -o client client.o
.PHONY:clean
clean:
	rm -rf *.o server client *.txt

