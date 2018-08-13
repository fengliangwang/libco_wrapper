

all:
	#g++ -o thread_workers_co -g -std=c++11 -DUSING_GLOG test.cpp thread_workers_co.cpp -lpthread -levent -lglog -I./libco/include -L./libco/lib -lcolib -ldl # with glog
	g++ -o thread_workers_co -g -std=c++11 test.cpp thread_workers_co.cpp -lpthread -levent -I./libco/include -L./libco/lib -lcolib -ldl
