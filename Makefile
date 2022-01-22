CFLAGS = -Wall -g  -std=gnu99
#  -m32
CC = gcc
#CC = clang

BINDIR = bin
OBJSDIR = objs

DEPS = server.h

OBJS = $(OBJSDIR)/server.o \
	$(OBJSDIR)/string.o \
	$(OBJSDIR)/config.o \
	$(OBJSDIR)/chunk.o \
	$(OBJSDIR)/threads_manager.o \
	$(OBJSDIR)/event_handler.o \
	$(OBJSDIR)/response.o \
	$(OBJSDIR)/create_socket.o \
	$(OBJSDIR)/percent_coding.o \
	$(OBJSDIR)/ranges.o \
	$(OBJSDIR)/rd_wr.o \
	$(OBJSDIR)/send_headers.o \
	$(OBJSDIR)/functions.o \
	$(OBJSDIR)/log.o \
	$(OBJSDIR)/cgi.o \
	$(OBJSDIR)/fcgi.o \
	$(OBJSDIR)/index.o \

server: $(OBJS)
	$(CC) $(CFLAGS) -o $@  $(OBJS) -lpthread

$(OBJSDIR)/server.o: server.c server.h
	$(CC) $(CFLAGS) -c server.c -o $@

$(OBJSDIR)/string.o: string.c server.h
	$(CC) $(CFLAGS) -c string.c -o $@

$(OBJSDIR)/config.o: config.c server.h 
	$(CC) $(CFLAGS) -c config.c -o $@

$(OBJSDIR)/chunk.o: chunk.c server.h
	$(CC) $(CFLAGS) -c chunk.c -o $@

$(OBJSDIR)/threads_manager.o: threads_manager.c server.h
	$(CC) $(CFLAGS) -c threads_manager.c -o $@

$(OBJSDIR)/event_handler.o: event_handler.c server.h
	$(CC) $(CFLAGS) -c event_handler.c -o $@

$(OBJSDIR)/response.o: response.c server.h 
	$(CC) $(CFLAGS) -c response.c -o $@

$(OBJSDIR)/create_socket.o: create_socket.c server.h
	$(CC) $(CFLAGS) -c create_socket.c -o $@

$(OBJSDIR)/percent_coding.o: percent_coding.c server.h
	$(CC) $(CFLAGS) -c percent_coding.c -o $@

$(OBJSDIR)/ranges.o: ranges.c server.h
	$(CC) $(CFLAGS) -c ranges.c -o $@

$(OBJSDIR)/rd_wr.o: rd_wr.c server.h
	$(CC) $(CFLAGS) -c rd_wr.c -o $@

$(OBJSDIR)/send_headers.o: send_headers.c server.h
	$(CC) $(CFLAGS) -c send_headers.c -o $@

$(OBJSDIR)/functions.o: functions.c server.h
	$(CC) $(CFLAGS) -c functions.c -o $@

$(OBJSDIR)/log.o: log.c server.h
	$(CC) $(CFLAGS) -c log.c -o $@

$(OBJSDIR)/cgi.o: cgi.c server.h 
	$(CC) $(CFLAGS) -c cgi.c -o $@

$(OBJSDIR)/fcgi.o: fcgi.c server.h 
	$(CC) $(CFLAGS) -c fcgi.c -o $@

$(OBJSDIR)/index.o: index.c server.h 
	$(CC) $(CFLAGS) -c index.c -o $@

clean:
	rm -f server
	rm -f $(OBJSDIR)/*.o
	rm -f *.o 
