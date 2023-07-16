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
	$(OBJSDIR)/threads_manager.o \
	$(OBJSDIR)/event_handler.o \
	$(OBJSDIR)/response.o \
	$(OBJSDIR)/socket.o \
	$(OBJSDIR)/percent_coding.o \
	$(OBJSDIR)/ranges.o \
	$(OBJSDIR)/create_headers.o \
	$(OBJSDIR)/functions.o \
	$(OBJSDIR)/log.o \
	$(OBJSDIR)/cgi.o \
	$(OBJSDIR)/fcgi.o \
	$(OBJSDIR)/scgi.o \
	$(OBJSDIR)/index.o \

server: $(OBJS)
	$(CC) $(CFLAGS) -o $@  $(OBJS) -lpthread

$(OBJSDIR)/server.o: server.c server.h
	$(CC) $(CFLAGS) -c server.c -o $@

$(OBJSDIR)/string.o: string.c server.h
	$(CC) $(CFLAGS) -c string.c -o $@

$(OBJSDIR)/config.o: config.c server.h
	$(CC) $(CFLAGS) -c config.c -o $@

$(OBJSDIR)/threads_manager.o: threads_manager.c server.h
	$(CC) $(CFLAGS) -c threads_manager.c -o $@

$(OBJSDIR)/event_handler.o: event_handler.c server.h
	$(CC) $(CFLAGS) -c event_handler.c -o $@

$(OBJSDIR)/response.o: response.c server.h
	$(CC) $(CFLAGS) -c response.c -o $@

$(OBJSDIR)/socket.o: socket.c server.h
	$(CC) $(CFLAGS) -c socket.c -o $@

$(OBJSDIR)/percent_coding.o: percent_coding.c server.h
	$(CC) $(CFLAGS) -c percent_coding.c -o $@

$(OBJSDIR)/ranges.o: ranges.c server.h
	$(CC) $(CFLAGS) -c ranges.c -o $@

$(OBJSDIR)/create_headers.o: create_headers.c server.h
	$(CC) $(CFLAGS) -c create_headers.c -o $@

$(OBJSDIR)/functions.o: functions.c server.h
	$(CC) $(CFLAGS) -c functions.c -o $@

$(OBJSDIR)/log.o: log.c server.h
	$(CC) $(CFLAGS) -c log.c -o $@

$(OBJSDIR)/cgi.o: cgi.c server.h
	$(CC) $(CFLAGS) -c cgi.c -o $@

$(OBJSDIR)/fcgi.o: fcgi.c server.h
	$(CC) $(CFLAGS) -c fcgi.c -o $@

$(OBJSDIR)/scgi.o: scgi.c server.h
	$(CC) $(CFLAGS) -c scgi.c -o $@

$(OBJSDIR)/index.o: index.c server.h
	$(CC) $(CFLAGS) -c index.c -o $@

clean:
	rm -f server
	rm -f $(OBJSDIR)/*.o
	rm -f *.o
