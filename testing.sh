echo -e "GET /httptest/index.html HTTP/1.0\r\nHost: localhost:8000\r\nConnection: close\r\n\r\n" | nc localhost 8000

echo -e "GET /httptest/user?user=Tia@gmail.com HTTP/1.0\r\nHost: localhost:8000\r\nConnection: close\r\n\r\n" | nc localhost 8000

gcc -shared -fPIC -I./include -o libmylib.so src/http.c src/fsm.c src/networking.c src/utils.c src/database.c
