TARGET = chat

CC = gcc
LIBS = -pthread
OBJS = chat.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LIBS) -o $(TARGET) $(OBJS)

clean: 
	rm -f *.o $(TARGET)
