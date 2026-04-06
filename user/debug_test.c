#include "kernel/types.h"
#include "user/user.h"

int main() {
    struct vmstats s;
    getvmstats(getpid(), &s);
    printf("DEBUG: faults=%d evicted=%d resident=%d\n", s.page_faults, s.pages_evicted, s.resident_pages);
    exit(0);
}
