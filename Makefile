CC:=g++

ExecFile2_1:=socks_server
ExecFile2_2:=hw4.cgi

MainFile2_1:=socks_server.cpp
MainFile2_2:=hw4.cpp

add:
	$(CC) -std=c++11 -pthread -o $(ExecFile2_1) $(MainFile2_1)
	$(CC) -std=c++11 -pthread -o $(ExecFile2_2) $(MainFile2_2)

%.o:%.cpp
	$(CC) -c $^ -o $@

.PHONY:clean
clean:
	rm $(ExecFile2_2)