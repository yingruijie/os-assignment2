#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>

#define TYPE_ADDR uint16_t  // type addresses
#define TYPE_PN   uint8_t   // type page number
#define TYPE_OFFS uint8_t   // type offset
#define TYPE_FN   uint8_t   // type frame number


#define SIZE_OF_PHYSICS    size_of_physics
#define SIZE_OF_FRAME      256
#define SIZE_OF_TLB        16
#define SIZE_OF_PAGE_TABLE 256
#define PRINT_DETAIL        0 // 0: print details of addresses to run diff
#define PRINT_RATE          1 // 1: print only tlb_hit_rate and page_fault rate
typedef struct node{
    uint8_t page;
    uint8_t frame;
    struct node* prior;
    struct node* next;
}table_t;

typedef enum ag{ fifo = 0, lru = 1, }algorithm_e;

FILE *file_addresses, *file_backing_store;
algorithm_e algorithm = fifo;
uint32_t size_of_physics = 256; 

uint8_t print_type = PRINT_DETAIL; 
uint16_t  addresses_logical = 0;
uint8_t**   addresses_physics;


table_t*  tlb_head = NULL;
table_t*  tlb_last = NULL;
table_t*  page_table_head = NULL;
table_t*  page_table_last = NULL;

uint32_t   tlb_hit = 0;
uint32_t   page_fault = 0;
uint32_t   tlb_node_number = 0;
uint32_t   page_table_node_number = 0;
uint32_t   addresses_physics_frame_used = 0;



void read_backing_store(uint8_t page, uint8_t frame);
void get_page(uint16_t address_logigcal);

void tlb_replace(uint8_t page, uint8_t frame);
void page_table_replace(uint8_t page, uint8_t *frame);

uint8_t table_list_search(uint8_t page, table_t** head, table_t** last, algorithm_e ag, uint8_t* frame_out);

int main(int argc, char**argv){ 
    // maybe argc = 1 in linux!
    // printf("arcg = %d\n", argc);
    if(argc < 3){
        printf("usage: ./vm [backing_store] [addresses.txt] -a[args1] -b[args2] -c[args3]\n");
        return -1;
    }

    file_addresses = fopen(argv[2], "r");
    if(NULL == file_addresses){
        printf("failed to open %s!\n", argv[2]); 
        return -1;
    }
                              
    file_backing_store = fopen(argv[1], "rb");
    if(NULL == file_backing_store){
        printf("failed to open %s!\n", argv[1]); 
        return -1;
    }

    int ch;
    int arg;   // -c argment size_of_physics 
    while ((ch = getopt(argc, argv, "a:b:c:d:")) != -1){
        switch (ch){
            case 'a':
                fclose(file_addresses);
                file_addresses = fopen(optarg, "r");
                if(NULL == file_addresses){
                    printf("-a arg is invalid, failed to open %s!\n", optarg); 
                    return -1;
                }
                break;
            case 'b':
                // printf("%s\n", optarg);
                if(strcmp(optarg, "lru") == 0){
                    algorithm = lru;
                }
                else if(strcmp(optarg, "fifo") == 0){
                    algorithm = fifo;
                }
                else{
                    printf("-b arg is invalid, please input lru or fifo!\n");
                    return -1;
                }
                break;
            case 'c':
                arg = atoi(optarg);
                if(arg >= 8 && arg <= 256){
                    size_of_physics = (uint32_t)arg;
                }
                else{
                    printf("-c arg is invalid, please input a size in [8, 256]!\n");
                    return -1;
                }
                break;
            case 'd':
                if(strcmp(optarg, "detail") == 0){
                    print_type = PRINT_DETAIL;
                }
                else if(strcmp(optarg, "rate") == 0){
                    print_type = PRINT_RATE;
                }
                else{
                    printf("-d arg is invalid, please input 'detail' or 'rate'!\n");
                    return -1;
                }
                break;
            case '?':
                printf("Unknown option: %c\n",(char)optopt);
                return -1;
                break;
        }
    }
    addresses_physics = (uint8_t**)malloc(sizeof(uint8_t*)*size_of_physics);
    for(int ii = 0; ii < size_of_physics; ii++){
        addresses_physics[ii] = (uint8_t*)malloc(sizeof(uint8_t)*SIZE_OF_FRAME);
        for(int jj = 0; jj < SIZE_OF_FRAME; jj++){
            addresses_physics[ii][jj] = 0;
        }
    }
    int num_of_addr = 0;
    while(fscanf(file_addresses, "%hu", &addresses_logical) != EOF){
        get_page(addresses_logical);
        num_of_addr++;
    }
    // printf("tlb_hit = %d\npage_fault = %d\n",tlb_hit, page_fault);
    printf("tlb_hit_rate = %f\npage_fault_rate = %f\n", 
    ((float)tlb_hit)/((float)num_of_addr), ((float)page_fault)/((float)num_of_addr));
    fclose(file_addresses);
    fclose(file_backing_store);
    return 0;
}


void get_page(uint16_t address_logical){
    uint8_t page_number = ((address_logical & 0xFFFF) >> 8);
    uint8_t offset = (address_logical & 0xFF);
    uint8_t frame_number = 0;
    uint8_t flag_tlb = 0, flag_page_table = 0;

    //check tlb
    flag_tlb = table_list_search(page_number, &tlb_head, &tlb_last, algorithm, &frame_number);
    
    // check the page table
    flag_page_table = table_list_search(page_number, &page_table_head, &page_table_last, algorithm, &frame_number);
    
    // page fault
    if(flag_page_table){ page_table_replace(page_number, &frame_number); page_fault++;}
    
    // printf("test3\n");
    if(flag_tlb) tlb_replace(page_number, frame_number);
    
    else tlb_hit++;
    //printf("test4\n");
    

    int8_t value = addresses_physics[frame_number][offset];
    
    if(print_type == PRINT_DETAIL)
        printf("Virtual address: %d Physical address: %d Value: %d\n", address_logical, (frame_number << 8) | offset, value);
}

void tlb_replace(uint8_t page, uint8_t frame){
    table_t* new_node = (table_t*)malloc(sizeof(table_t));
    new_node->frame = frame;
    new_node->page = page;
    new_node->next = NULL;
    new_node->prior = NULL;
    // page is not in tlb
    if(tlb_node_number == 0){
        tlb_head = new_node;
        tlb_last = new_node;
    }
    else{
        if(tlb_node_number == SIZE_OF_TLB){
            table_t* p = tlb_last;
            tlb_last = tlb_last->prior;
            tlb_last->next = NULL;
            free(p);
        }
        // insert node to head
        tlb_head->prior = new_node;
        new_node->next = tlb_head;
        tlb_head = new_node;
    }
    // increase node number
    if(tlb_node_number < SIZE_OF_TLB) tlb_node_number++;
}

void page_table_replace(uint8_t page, uint8_t *frame){
    table_t* new_node = (table_t*)malloc(sizeof(table_t));
    new_node->page = page;
    new_node->next = NULL;
    new_node->prior = NULL;

    // page is not in tlb
    if(page_table_node_number == SIZE_OF_PAGE_TABLE ||
       addresses_physics_frame_used == SIZE_OF_PHYSICS){
        table_t* p = page_table_last;
        page_table_last = page_table_last->prior;
        page_table_last->next = NULL;
        // p->frame
        new_node->frame = p->frame;
        free(p);
        page_table_head->prior = new_node;
        new_node->next = page_table_head;
        page_table_head = new_node;
    }
    else if(page_table_node_number == 0){
        new_node->frame = addresses_physics_frame_used;
        page_table_head = new_node;
        page_table_last = new_node;
        addresses_physics_frame_used++;
        page_table_node_number++;
    }
    else{
        new_node->frame = addresses_physics_frame_used;
        page_table_head->prior = new_node;
        new_node->next = page_table_head;
        page_table_head = new_node;
        addresses_physics_frame_used++;
        page_table_node_number++;
    }
    read_backing_store(new_node->page, new_node->frame);
    *frame = new_node->frame;
}

uint8_t table_list_search(uint8_t page, table_t** p_head, table_t** p_last, algorithm_e ag, uint8_t* frame_out){
    table_t* head = *p_head; table_t* last = *p_last;
    
    table_t* p = head;
    int ii = 0;
    while(p != NULL){
        if(p->page == page){
            *frame_out = p->frame;
            if((ag == lru) && (p != head)){
                // take p to head of list
                if(p == last){ last = p->prior;}
                else {p->next->prior = p->prior;}
                p->prior->next = p->next;
                p->prior = NULL;
                p->next = head;
                head->prior = p;
                head = p;
            }
            *p_head = head; *p_last = last;
            return 0;
        }
        p = p->next;
    }
    *p_head = head; *p_last = last;
    return 1;
}

/**
 * pn: page number
 * */
void read_backing_store(uint8_t page, uint8_t frame){
    int8_t  buffer[SIZE_OF_FRAME];
    fseek(file_backing_store, ((int)page) * SIZE_OF_FRAME, SEEK_SET);
    fread(buffer, sizeof(int8_t), SIZE_OF_FRAME, file_backing_store);
    for(int ii = 0; ii < SIZE_OF_FRAME; ii++){
        addresses_physics[frame][ii] = buffer[ii];
    }
}