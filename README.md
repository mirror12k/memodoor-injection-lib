# MemoDoor Injection Library
MemoDoor ("Memory-Door") is a debugging library for remote DMA and injection.
Inject it into processes with `LD_PRELOAD=./memodoor_inject.so bash` to get it running.
When memodoor starts, it spins up a new thread inside the process and opens an http server on port 8999 (set the `MEMODOOR_PORT` env variable to change this).
Communicate with the server directly on http://localhost:8999

**BEWARE: memodoor is (intentionally) a memory-unsafe library. Use it only for debugging in secure environments. Do not expose it to the public internet!!!**

## Endpoints
The MemoDoor http server features the following utility endpoints:

- `/` : hello world
- `/maps` : returns `/proc/self/maps`, e.g. `curl -s http://localhost:8999/maps`
- `/env` : returns the current environment variables in the process, e.g. `curl -s http://localhost:8999/env`
- `/threads` : returns a list of threads in the current process, e.g. `curl -s http://localhost:8999/threads`
- `/process` : returns the process id and command line of the current process, e.g. `curl -s http://localhost:8999/process`
- `/memory/{}` : reads memory directly from the process, returns the contents, e.g. `curl -s http://localhost:8999/memory/7f68d35ea1:f00 | hexdump -C`
- `PUT /memory/{}` : writes the request body directly to the memory address specified, e.g. `curl -s http://localhost:8999/memory/9f50edf050:a -X PUT -d 'helloworldhello'`
- `/exec/{}` : executes the function at the given address, useful for testing shellcode
- `/mmap/{}` : calls mmap with the given parameters, returns the result pointer, e.g. `curl http://localhost:8999/mmap/0:40000:rw-`
- `/search/{}` : searches the process' memory space for a given search string (specified as hexidecimal), e.g. `curl http://localhost:8999/search/2f64617368`
- `/dlsym/{}` : calls dlsym with the string argument, returns the result pointer, e.g. `curl http://localhost:8999/dlsym/printf`
- `/dlopen/{}` : calls dlopen with the given string argument
- `PUT /python` : executes the body of the request as a python program in the current process space, e.g. `curl -s http://localhost:8999/python -X PUT -d 'import sys; import os; print("hello world!", os.getpid())'`

## Notes
Pull requests and forks welcome. Tell me if you found this tool useful!


