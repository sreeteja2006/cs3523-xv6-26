#define NPROC 64                    // maximum number of processes
#define NCPU 8                      // maximum number of CPUs
#define NOFILE 16                   // open files per process
#define NFILE 100                   // open files per system
#define NINODE 50                   // maximum number of active i-nodes
#define NDEV 10                     // maximum major device number
#define ROOTDEV 1                   // device number of file system root disk
#define MAXARG 32                   // max exec arguments
#define MAXOPBLOCKS 10              // max # of blocks any FS op writes
#define LOGBLOCKS (MAXOPBLOCKS * 3) // max data blocks in on-disk log
#define NBUF (MAXOPBLOCKS * 3)      // size of disk block cache
#define FSSIZE 4000                 // size of file system in blocks
#define MAXPATH 128                 // maximum file path name
#define USERSTACK 1                 // user stack pages
#define SWAP_START_BLOCK 4000       // block number of first swap page
#define SWAP_DISK_BLOCKS 120000       // number of blocks in swap disk
#define NDISK 4
#define BLOCKS_PER_DISK (SWAP_DISK_BLOCKS / NDISK)
#define RAID_MODE_0 0
#define RAID_MODE_1 1
#define RAID_MODE_5 5
#define DISK_SCHED_FCFS 0
#define DISK_SCHED_SSTF 1
#define ROTATIONAL_DELAY 7
