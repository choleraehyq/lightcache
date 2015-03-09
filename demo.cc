#include <iostream>
#include <functional>

#include "lightcache.h"

int main() {
	lightcache::cache<int, char> test("demo.out", 5, 5, std::hash<int>());
	test.set(1, 'a');
	std::cout << "set 1 done\n";
	test.set(2, 'b');
	test.set(3, 'c');
	std::cout << "set done" << std::endl;
	test.del(2);
	std::cout << "del done" << std::endl;
	std::cout << "count: " << test.count() << std::endl;
	test.set(4, 'd');
	std::cout << "get: " << test.get(2, '?') << std::endl;
	std::cout << "exist: "<< test.exist(1) << std::endl;
	return 0;
}
