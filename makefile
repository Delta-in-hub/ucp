main :
	@g++ -Ofast -std=c++17 ucp.cc -o ucp
test : main
	time ./ucp ~/Videos ./test
	time ./ucp ~/Documents/484192.mp4 ./
	time ./ucp ~/zerofile ./
	@rm -r ./test
	@rm ./484192.mp4
	@rm ./zerofile
