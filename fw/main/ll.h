#pragma once

#include "stdint.h"

#define MAX_RX_LEN (16)
#define MAX_TX_LEN (16)
#define MAX_CR_LEN (16)

#define TAKE_SEM      (1)
#define DONT_TAKE_SEM (0)

#define STORE_DATA      (1)
#define DONT_STORE_DATA (1)

typedef struct LinkedList {
    void*              data;
    uint16_t           transaction_id;
    size_t             size;
    uint8_t            retry_count;  // External (how many times it got sent _out_ of TCP core)
    uint8_t            internal_ack; // internal (TCP socket writter gave us an ack that it sent this packout out)
    uint64_t           time_stamp;
    bool               store; // If LL should store the data
    struct LinkedList* next;
} node_t;

typedef enum {
    RX_LL,
    TX_LL,
    CR_LL
} ll_type_e;
typedef enum {
    INTERNAL_ACK,
    INCREMENT_RETY_COUNT
} ll_action_e;

//crc
uint16_t crc16(uint8_t* buf, size_t len);
uint32_t crc32(const void* buf, size_t size);

//test only
void ll_test();
void print_debug(ll_type_e type);

void ll_init();
int  ll_pop(ll_type_e type, void* data);
int  ll_delete(const ll_type_e type, uint16_t transaction_id, bool take_sem);
int  ll_add_node(ll_type_e type, void* data, size_t size, uint16_t transaction_id, bool store);
int  ll_get_counter(ll_type_e type);
int  ll_modify(uint16_t transaction_id, ll_action_e action, const ll_type_e type);
int  ll_peek(uint16_t transaction_id, bool take_sem, uint8_t* data);

// Walk related stuff
int     ll_walk_reset(const ll_type_e type);
node_t* walk_ll(const ll_type_e type);

// Mutex stuff
void take_ll_sem(const ll_type_e type);
void give_ll_sem(const ll_type_e type);
