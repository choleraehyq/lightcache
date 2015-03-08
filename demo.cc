#include <iostream>
#include <functional>

#include "lightcache.h"

int main() {
	lightcache::cache<int, std::string> test("a.txt", 10, 10, std::hash<int>());
	test.set(1, std::string("a"));
	test.set(2, std::string("b"));
	test.set(3, std::string("c"));
	std::cout << "set done" << std::endl;
	test.del(2);
	std::cout << "del done" << std::endl;
	std::cout << "count: " << test.count() << std::endl;
	test.set(4, std::string("d"));
	std::cout << "get: " << test.get(2, std::string("shit")) << std::endl;
	std::cout << "exist: "<< test.exist(1) << std::endl;
	return 0;
}
