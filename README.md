Lightcache implements LRU algorithm.

All procedures are thread-safe. You can uses one cache across many threads.

TO FIX:

1. The data cached in memory should be mapped to a file on the disk, so that the cached data can be used again correctly after the process restart. But now when I open a file again, the file will be filled by 0 except the very front of file.

2. Now the key element and the value element can't be variable-length.

3. Memory pool of lrulist. malloclru() and freelru()
