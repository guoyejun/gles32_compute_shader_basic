gl3_cs_basic: gl3_cs_basic.cpp
	g++ $< -g -O0 -lGLESv2 -lEGL -o $@

clean:
	rm -rf gl3_cs_basic
