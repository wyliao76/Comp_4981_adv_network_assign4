echo -e "GET /httptest/index.html HTTP/1.0\r\nHost: localhost:8000\r\nConnection: close\r\n\r\n" | nc localhost 8000

gcc -shared -fPIC -I./include -o http.so src/http.c src/fsm.c
