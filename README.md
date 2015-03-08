The lightcache implements LRU algorithm.

The data cached in memory will be mapped to a file on the disk, so the cached data can be used again correctly after the process restart.

All procedures are thread-safe. You can uses one cache across many threads.
