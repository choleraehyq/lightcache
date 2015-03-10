#include <iostream>
#include <functional>

#include "lightcache.h"

#define firstuse

int main() {
	lightcache::cache<int, char> test("demo.out", 5, 5, std::hash<int>());
#ifdef firstuse
	test.set(1, 'a');
	std::cout << "set 1 done\n";
	test.set(2, 'b');
	std::cout << "set 2 done\n";
	test.set(3, 'c');
	std::cout << "set 3 done\n";
	test.del(2);
	std::cout << "del done" << std::endl;
	std::cout << "count: " << test.count() << std::endl;
	test.set(4, 'd');
	std::cout << "getOrElse: " << test.getOrElse(2, '?') << std::endl;
	std::cout << "exist: "<< test.exist(1) << std::endl;
	test.set(5, 'e');
	test.set(6, 'f');
	test.set(7, 'g');
	std::cout << "set 7 done\n";
	std::cout << "getOrElse7: "<< test.getOrElse(7, '*') << std::endl;
	std::cout << "getOrElse1: "<< test.getOrElse(1, '.') << std::endl;
#else
	std::cout << "getOrElse5: "<< test.getOrElse(3, '.') << std::endl;
	std::cout << "count: " << test.count() << std::endl;
#endif
	return 0;
}
