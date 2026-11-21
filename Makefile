# PC simulator (no hardware needed): make sim && make shots
CXX ?= g++
CORE = core/cui_fb.cpp core/cui_rns.cpp core/cui_core.cpp core/cui_render.cpp core/cui_splash.cpp core/city/CityLocator.cpp

sim/sim: sim/sim.cpp $(CORE) core/*.h
	$(CXX) -std=c++17 -O2 -Wall -Icore -o $@ sim/sim.cpp $(CORE)

sim/trace.txt: sim/make_trace.py
	python3 sim/make_trace.py sim/trace.txt sim/demo-places.bin

shots: sim/sim sim/trace.txt
	rm -rf sim/out && mkdir -p sim/out/shots sim/out/frames sim/out/png
	./sim/sim sim/trace.txt sim/demo-places.bin sim/out
	python3 tools/frames_to_media.py sim/out

headers:
	python3 tools/gen_names.py core/cui_names.h
	python3 tools/gen_art.py core/cui_art.h
	python3 tools/gen_splash.py

splash: sim/splash_sim.cpp core/cui_splash.cpp core/cui_splash_art.h
	$(CXX) -std=c++17 -O2 -Wall -Icore -o sim/splash_sim sim/splash_sim.cpp core/cui_splash.cpp core/cui_fb.cpp
	mkdir -p sim/out/splash && ./sim/splash_sim sim/out/splash

.PHONY: shots headers splash
