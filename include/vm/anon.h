#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "devices/disk.h"
struct page;
enum vm_type;

struct anon_page {
    //swap out 상태라면 swap disk 어디에 저장했는지?
    disk_sector_t sector_start;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
