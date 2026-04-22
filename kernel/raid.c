#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

static int raid_mode = RAID_MODE_0;
static int disk_failed[NDISK];

// Split the swap area into one physical region per RAID mode so that
// changing modes cannot overwrite pages that were written under a
// previous mode.
#define MODE_REGION_BLOCKS (SWAP_DISK_BLOCKS / 3)
#define MODE_DISK_BLOCKS (MODE_REGION_BLOCKS / NDISK)

static int mode_index(int mode)
{
    if (mode == RAID_MODE_0)
        return 0;
    if (mode == RAID_MODE_1)
        return 1;
    if (mode == RAID_MODE_5)
        return 2;
    return 0;
}

static int mode_base_block(int mode)
{
    return SWAP_START_BLOCK + mode_index(mode) * MODE_REGION_BLOCKS;
}

static int phys_block(int mode, int disk, int pos)
{
    if (disk < 0 || disk >= NDISK || pos < 0 || pos >= MODE_DISK_BLOCKS)
        panic("phys_block: bad index");
    return mode_base_block(mode) + disk * MODE_DISK_BLOCKS + pos;
}

static void sim_read(int mode, int disk, int pos, void *buf)
{
    if (disk_failed[disk])
    {
        memset(buf, 0, BSIZE);
        return;
    }
    struct buf *b = bread(ROOTDEV, phys_block(mode, disk, pos));
    memmove(buf, b->data, BSIZE);
    brelse(b);
}

static void sim_write(int mode, int disk, int pos, void *buf)
{
    if (disk_failed[disk])
        return;

    struct buf *b = bread(ROOTDEV, phys_block(mode, disk, pos));
    memmove(b->data, buf, BSIZE);
    bwrite(b);
    brelse(b);
}

static int logical_limit(int mode)
{
    if (mode == RAID_MODE_0)
        return MODE_REGION_BLOCKS;
    if (mode == RAID_MODE_1)
        return 2 * MODE_DISK_BLOCKS;
    if (mode == RAID_MODE_5)
        return 3 * MODE_DISK_BLOCKS;
    return MODE_REGION_BLOCKS;
}

static void raid5_location(int rel, int *data_disk_out,
                           int *parity_disk_out, int *stripe_out)
{
    int stripe = rel / (NDISK - 1);
    int pos_in_stripe = rel % (NDISK - 1);
    int parity_disk = stripe % NDISK;

    int data_disk = (pos_in_stripe < parity_disk) ? pos_in_stripe : pos_in_stripe + 1;

    *data_disk_out = data_disk;
    *parity_disk_out = parity_disk;
    *stripe_out = stripe;
}

static void raid1_location(int rel, int *primary_out, int *secondary_out,
                           int *pos_out)
{
    int pair = rel / MODE_DISK_BLOCKS;
    *pos_out = rel % MODE_DISK_BLOCKS;
    *primary_out = (pair % 2) * 2;
    *secondary_out = *primary_out + 1;
}

void raid_read(int logical_block, void *data, int mode)
{
    int rel = logical_block - SWAP_START_BLOCK;
    if (rel < 0 || rel >= logical_limit(mode))
        panic("raid_read: logical block out of range");

    switch (mode)
    {

    case RAID_MODE_0:
        if (disk_failed[rel % NDISK])
            panic("RAID 0 read failed disk");
        sim_read(mode, rel % NDISK, rel / NDISK, data);
        break;

    case RAID_MODE_1:
    {
        int primary, secondary, pos;
        raid1_location(rel, &primary, &secondary, &pos);

        if (!disk_failed[primary])
            sim_read(mode, primary, pos, data);
        else if (!disk_failed[secondary])
            sim_read(mode, secondary, pos, data);
        else
            panic("RAID 1 read: both disks failed");
    }
    break;

    case RAID_MODE_5:
    {
        int data_disk, parity_disk, stripe;
        raid5_location(rel, &data_disk, &parity_disk, &stripe);

        if (!disk_failed[data_disk])
        {
            sim_read(mode, data_disk, stripe, data);
        }
        else
        {
            memset(data, 0, BSIZE);
            char *tmp = kalloc();
            if (tmp == 0)
                panic("RAID 5 read: kalloc tmp");
            for (int i = 0; i < NDISK; i++)
            {
                if (i == data_disk)
                    continue;
                if (disk_failed[i])
                    panic("RAID 5 read: multiple disks failed");
                sim_read(mode, i, stripe, tmp);
                for (int j = 0; j < BSIZE; j++)
                    ((char *)data)[j] ^= tmp[j];
            }
            kfree(tmp);
        }
    }
    break;

    default:
        sim_read(mode, rel % NDISK, rel / NDISK, data);
        break;
    }
}

void raid_write(int logical_block, void *data, int mode)
{
    int rel = logical_block - SWAP_START_BLOCK;
    if (rel < 0 || rel >= logical_limit(mode))
        panic("raid_write: logical block out of range");

    switch (mode)
    {

    case RAID_MODE_0:
        if (disk_failed[rel % NDISK])
            panic("RAID 0 write failed disk");
        sim_write(mode, rel % NDISK, rel / NDISK, data);
        break;

    case RAID_MODE_1:
    {
        int primary, secondary, pos;
        raid1_location(rel, &primary, &secondary, &pos);

        if (disk_failed[primary] && disk_failed[secondary])
            panic("RAID 1 write: both disks failed");

        if (!disk_failed[primary])
            sim_write(mode, primary, pos, data);
        if (!disk_failed[secondary])
            sim_write(mode, secondary, pos, data);
    }
    break;

    case RAID_MODE_5:
    {
        int data_disk, parity_disk, stripe;
        raid5_location(rel, &data_disk, &parity_disk, &stripe);

        if (disk_failed[data_disk])
        {
            if (disk_failed[parity_disk])
                panic("RAID 5 write: multiple disks failed");
            struct buf *bp = bread(ROOTDEV, phys_block(mode, parity_disk, stripe));

            for (int i = 0; i < BSIZE; i++)
                bp->data[i] = ((char *)data)[i];

            for (int i = 0; i < NDISK; i++)
            {
                if (i == data_disk || i == parity_disk)
                    continue;
                if (disk_failed[i])
                    panic("RAID 5 write: multiple disks failed");
                struct buf *bd = bread(ROOTDEV, phys_block(mode, i, stripe));
                for (int j = 0; j < BSIZE; j++)
                    bp->data[j] ^= bd->data[j];
                brelse(bd);
            }
            bwrite(bp);
            brelse(bp);
        }
        else if (disk_failed[parity_disk])
        {
            struct buf *bd = bread(ROOTDEV, phys_block(mode, data_disk, stripe));
            memmove(bd->data, data, BSIZE);
            bwrite(bd);
            brelse(bd);
        }
        else
        {
            int first_disk = (data_disk < parity_disk) ? data_disk : parity_disk;
            int second_disk = (data_disk < parity_disk) ? parity_disk : data_disk;

            struct buf *bf = bread(ROOTDEV, phys_block(mode, first_disk, stripe));
            struct buf *bs = bread(ROOTDEV, phys_block(mode, second_disk, stripe));

            struct buf *bd = (data_disk == first_disk) ? bf : bs;
            struct buf *bp = (parity_disk == first_disk) ? bf : bs;

            for (int i = 0; i < BSIZE; i++)
                bp->data[i] ^= bd->data[i] ^ ((char *)data)[i];

            memmove(bd->data, data, BSIZE);

            bwrite(bp);
            bwrite(bd);

            brelse(bs);
            brelse(bf);
        }
    }
    break;

    default:
        sim_write(mode, rel % NDISK, rel / NDISK, data);
        break;
    }
}

void raid_set_mode(int mode)
{
    raid_mode = mode;
}

int raid_get_mode(void)
{
    return raid_mode;
}

void faildisk(int disk)
{
    if (disk >= 0 && disk < NDISK)
        disk_failed[disk] = 1;
}

void recovereddisk(int disk)
{
    if (disk >= 0 && disk < NDISK)
        disk_failed[disk] = 0;
}
