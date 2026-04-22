typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long uint64;

typedef uint64 pde_t;
typedef struct mlfqinfo
{
  int level;
  int ticks[4];
  int times_scheduled;
  int total_syscalls;
} mlfqinfo;

typedef struct vmstats
{
  int page_faults;
  int pages_evicted;
  int pages_swapped_in;
  int pages_swapped_out;
  int resident_pages;
  int disk_reads;
  int disk_writes;
  int avg_disk_latency;
} vmstats;