g++ -c -o runtime.opengl_host.o runtime.opengl_host.cpp -I/usr/include
g++ -o test-gl runtime.opengl_host.o -L/usr/lib -lGL -lglut -lGLEW -lm 