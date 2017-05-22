# malloc
98 percent utilization using rb tree data structure


Introduction

The purpose of this lab is to write a custom dynamic storage allocator for C programs, i.e., our own version of the malloc, free, and realloc routines. The overall goal is to implement a correctly working, fast, and efficient allocator.

Logistics

The dynamic storage allocator will consist of the following four functions, which are declared in mm.h and defined in mm.c: (1) int mm_init(void); (2) void *mm_malloc(size_t size); (3) void mm_free(void *ptr); (4) void *mm_realloc(void *ptr, size_t size);
The semantics outlined in the handout match the semantics of the corresponding libc malloc, realloc, and free routines. Type man malloc to the shell for complete documentation.
Trace-Driven Driver Program
The driver program mdriver.c in the source code distribution tests the mm.c package for correctness, space utilization, and throughput. Each trace file contains a sequence of allocate, reallocate, and free directions that instruct the driver to call the mm_malloc, mm_realloc, and mm_free routines in some sequence.
The driver mdriver.c accepts the following command line arguments:

./mdriver -t -f -h -I -v -V -d n



***********
Main Files:
***********

mdriver
        Once you've run make, run ./mdriver to test your solution.

traces/
	Directory that contains the trace files that the driver uses
	to test your solution. Files orners.rep, short2.rep, and malloc.rep
	are tiny trace files that you can use for debugging correctness.

**********************************
Other support files for the driver
**********************************

config.h	Configures the malloc lab driver
fsecs.{c,h}	Wrapper function for the different timer packages
clock.{c,h}	Routines for accessing the Pentium and Alpha cycle counters
fcyc.{c,h}	Timer functions based on cycle counters
ftimer.{c,h}	Timer functions based on interval timers and gettimeofday()
memlib.{c,h}	Models the heap and sbrk function
