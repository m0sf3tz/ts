#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "stdint.h"
#include <string.h>
#include <sys/param.h>

#include "ll.h"
#include "system_defines.h"
#include "timer_helper.h"

/**********************************************************
*                LL - CORE PRIVATE VARIABLES
**********************************************************/

static SemaphoreHandle_t rx_sem, tx_sem, cr_sem;
static const char        TAG[] = "LL";                    // TAG for ESP prints
static node_t *          root_rx, *root_tx, *root_cr;     // the roots of respective LL
static node_t *          cur_node_tx, *cur_node_cr;       // Used in peek
static int               len_rx_ll, len_tx_ll, len_cr_ll; // length of respective L

static const uint32_t crc32Table[256] = {
    0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
    0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
    0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
    0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
    0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
    0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
    0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
    0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
    0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
    0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
    0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
    0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
    0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
    0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
    0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
    0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
    0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
    0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
    0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
    0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
    0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
    0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
    0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
    0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
    0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
    0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
    0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
    0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
    0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
    0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
    0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
    0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
    0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
    0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
    0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
    0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
    0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
    0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
    0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
    0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
    0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
    0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
    0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
    0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
    0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
    0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
    0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
    0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
    0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
    0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
    0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
    0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
    0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
    0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
    0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
    0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
    0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
    0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
    0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
    0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
    0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
    0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
    0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
    0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L
};

uint32_t crc32(const void* buf, size_t size) {
    const uint8_t* p   = buf;
    uint32_t       crc = 0xBABE;

    while (size--)
        crc = crc32Table[(crc ^ *p++) & 0xff] ^ (crc >> 8);

    return crc;
}

static const unsigned short crc16tab[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

/**********************************************************
*                LL - CORE PUBLIC FUNCTIONS
**********************************************************/
int ll_get_counter(ll_type_e type) {
    int ret = 0;
    if (type == RX_LL) {
        xSemaphoreTake(rx_sem, portMAX_DELAY);
        ret = len_rx_ll;
        xSemaphoreGive(rx_sem);
    } else if (type == TX_LL) {
        xSemaphoreTake(tx_sem, portMAX_DELAY);
        ret = len_tx_ll;
        xSemaphoreGive(tx_sem);
    } else {
        xSemaphoreTake(cr_sem, portMAX_DELAY);
        ret = len_cr_ll;
        xSemaphoreGive(cr_sem);
    }
    return ret;
}

uint16_t crc16(uint8_t* buf, size_t len) {
    int      counter;
    uint16_t crc = 0xDEAD;
    for (counter = 0; counter < len; counter++) {
        crc = (crc << 8) ^ crc16tab[((crc >> 8) ^ buf[counter]) & 0x00FF];
    }
    return crc;
}

static node_t* create_node(size_t size, void* data, uint16_t transaction_id, uint64_t time_stamp, bool store) {
    node_t* temp;
    // Create the node
    temp = (node_t*)malloc(sizeof(node_t));
    if (temp == NULL) {
        return NULL;
    }
    // heap variable, zero it out
    memset(temp, 0, sizeof(node_t));

    if (store) {
        //create the memory for the payload
        temp->data = malloc(size);
        if (temp->data == NULL) {
            return NULL;
        }
        memcpy(temp->data, data, size);
    }

    temp->retry_count    = 0;
    temp->size           = size;
    temp->next           = NULL;
    temp->transaction_id = transaction_id;
    temp->time_stamp     = time_stamp;
    temp->store          = store;

    return temp;
}

static int add_node(void* data, size_t size, node_t** root, uint16_t transaction_id, bool store) {
    node_t* head = *root;

    if (size == 0 || data == NULL) {
        return -1;
    }

    node_t *temp, *p;
    temp = create_node(size,
                       data,
                       transaction_id,
                       timer_get_ms_since_boot(), // Record when this node was added
                       store);
    if (temp == NULL) {
        return -1;
    }

    if (head == NULL) {
        *root = temp;
    } else {
        p = head;
        while (p->next != NULL) {
            p = p->next;
        }
        p->next = temp;
    }
    return transaction_id;
}

void print_debug(ll_type_e type) {
    node_t* temp;
    if (type == RX_LL) {
        temp = root_rx;
    } else if (type == TX_LL) {
        temp = root_tx;
    } else {
        temp = root_cr;
    }

    if (temp == NULL) {
        ESP_LOGI(TAG, "Can't print empty LL");
    }

    while (temp != NULL) {
        ESP_LOGI(TAG, "type of node = %d, transaction_id = %d, internal_ack = %d", *(uint8_t*)temp->data, temp->transaction_id, temp->internal_ack);
        temp = temp->next;
    }
}

// Modify a specific node in the ll (like in the middle)
static int modify(uint16_t transaction_id, node_t** root) {
    node_t* iter = *root;

    if (iter == NULL) {
        ESP_LOGE(TAG, "Error: root == null, can't modify");
        return -1;
    }

    for (;;) {
        if (iter->transaction_id == transaction_id) {
            iter->internal_ack = TRUE;
            return iter->internal_ack;
        }

        iter = iter->next;
        if (iter == NULL) {
            return -1;
        }
    }
}

// Modify a specific node in the ll (like in the middle)
int ll_modify(uint16_t transaction_id, ll_action_e action, const ll_type_e type) {
    if (type == TX_LL) {
        xSemaphoreTake(tx_sem, portMAX_DELAY);
        int ret = -1;

        if (action == INTERNAL_ACK) {
            ret = modify(transaction_id, &root_tx);
        }

        xSemaphoreGive(tx_sem);
        return ret;
    } else if (type == CR_LL) {
        xSemaphoreTake(cr_sem, portMAX_DELAY);
        int ret = -1;

        if (action == INTERNAL_ACK) {
            ret = modify(transaction_id, &root_cr);
        }

        xSemaphoreGive(cr_sem);
        return ret;
    } else {
        assert(0);
        return 0; // to stop the compiler from whining
    }
}

// Delete a specific node in the ll (like in the middle)
static int delete (uint16_t transaction_id, node_t** root) {
    node_t* leader = *root;

    if (leader == NULL) {
        ESP_LOGE(TAG, "Error: leader == null, can't delete transaction_id = %d", transaction_id);
        return -1;
    }
    // Must delete the memory being pointed at by the node (data)
    // and also the node itself - two frees required
    if (leader->transaction_id == transaction_id) {
        node_t*  temp           = leader->next;
        uint16_t transaction_id = leader->transaction_id;
        if (leader->store == true) {
            free(leader->data);
        }
        free(leader);
        *root = temp;
        return transaction_id;
    }

    node_t* follower = leader;

    while (1) {
        leader = leader->next;

        if (leader == NULL) {
            ESP_LOGE(TAG, "Could not find transaction_id %d", transaction_id);
            return -1;
        }

        if (leader->transaction_id == transaction_id) {
            node_t* temp           = leader->next;
            int     transaction_id = leader->transaction_id;
            if (leader->store == true) {
                free(leader->data);
            }
            free(leader);
            follower->next = temp;
            return transaction_id;
        }
        follower = leader;
    }
}

//pops head
static int pop(node_t** root, void* data) {
    node_t* head;
    head = *root;

    if (data == NULL) {
        ESP_LOGE(TAG, "data is a null pointer");
        ASSERT(0);
    }

    if (head == NULL) {
        ESP_LOGE(TAG, "ROOT is NULL!");
        ASSERT(0);
    }

    if (head->next == NULL) {
        //only one element in the LL
        *root                   = NULL;
        uint16_t transaction_id = head->transaction_id;
        memcpy(data, head->data, head->size);
        free(head->data);
        free(head);
        return transaction_id;
    }

    *root = head->next;

    uint16_t transaction_id = head->transaction_id;
    memcpy(data, head->data, head->size);
    free(head->data);
    free(head);
    return transaction_id;
}

// External Functions

// Returns -1 for error, transaction_id otherwise
int ll_pop(const ll_type_e type, void* data) {
    int ret = 0;

    if (type == RX_LL) {
        xSemaphoreTake(rx_sem, portMAX_DELAY);
        ret = pop(&root_rx, data);
        if (ret != -1) {
            len_rx_ll--;
        }
        xSemaphoreGive(rx_sem);
    } else if (type == TX_LL) {
        xSemaphoreTake(tx_sem, portMAX_DELAY);
        ret = pop(&root_tx, data);
        if (ret != -1) {
            len_tx_ll--;
        }
        xSemaphoreGive(tx_sem);
    } else {
        xSemaphoreTake(cr_sem, portMAX_DELAY);
        ret = pop(&root_cr, data);
        if (ret != -1) {
            len_cr_ll--;
        }
        xSemaphoreGive(cr_sem);
    }

    if (ret == -1) {
        ESP_LOGE(TAG, "Failed to pop que type %d \n", type);
    }
    return ret;
}

// Returns -1 for error, transaction_id otherwise
int ll_delete(ll_type_e type, uint16_t transaction_id, bool take_sem) {
    int ret = 0;
    if (type == RX_LL) {
        if (take_sem) {
            xSemaphoreTake(rx_sem, portMAX_DELAY);
        }
        ret = delete (transaction_id, &root_rx);
        if (ret != -1) {
            len_rx_ll--;
        }
        if (take_sem) {
            xSemaphoreGive(rx_sem);
        }
    } else if (type == TX_LL) {
        if (take_sem) {
            xSemaphoreTake(tx_sem, portMAX_DELAY);
        }
        ret = delete (transaction_id, &root_tx);
        if (ret != -1) {
            len_tx_ll--;
        }
        if (take_sem) {
            xSemaphoreGive(tx_sem);
        }
    } else {
        if (take_sem) {
            xSemaphoreTake(cr_sem, portMAX_DELAY);
        }
        ret = delete (transaction_id, &root_cr);
        if (ret != -1) {
            len_cr_ll--;
        }
        if (take_sem) {
            xSemaphoreGive(cr_sem);
        }
    }

    return transaction_id;
}

static int peek(uint16_t transaction_id, node_t** root, uint8_t* data) {
    node_t* leader = *root;

    if (leader == NULL) {
        ESP_LOGE(TAG, "Error: leader == null, can't peek transaction_id %d", transaction_id);
        return -1;
    }

    if (leader->transaction_id == transaction_id) {
        if (leader->store == true) {
            memcpy(data, leader->data, leader->size);
            return transaction_id;
        } else {
            return -1;
        }
    }

    node_t* follower = leader;

    while (1) {
        leader = leader->next;

        if (leader == NULL) {
            ESP_LOGE(TAG, "Could not find transaction_id %d", transaction_id);
            return -1;
        }

        if (leader->transaction_id == transaction_id) {
            if (leader->store == true) {
                memcpy(data, leader->data, leader->size);
                return transaction_id;
            } else {
                return -1;
            }
        }

        follower = leader;
    }
}

// Returns -1 for error, transaction_id otherwise
int ll_peek(uint16_t transaction_id, bool take_sem, uint8_t* data) {
    int ret = 0;
    if (take_sem) {
        xSemaphoreTake(cr_sem, portMAX_DELAY);
    }
    ret = peek(transaction_id, &root_cr, data);

    if (take_sem) {
        xSemaphoreGive(cr_sem);
    }
    return transaction_id;
}

// Spins until there is room in the LL we are
// interested in
static void spin_till_free(ll_type_e type) {

    uint32_t   delay = 1000 / portTICK_PERIOD_MS;
    BaseType_t rc;

    if (type == RX_LL) {
        while (1) {
            ESP_LOGI(TAG, "Adding a packet to the RX_LL, currently has %d", ll_get_counter(RX_LL));
            rc = xSemaphoreTake(rx_sem, delay);
            if (rc != pdTRUE) {
                ASSERT(0);
            }
            if (len_rx_ll == MAX_RX_LEN) {
                xSemaphoreGive(rx_sem);
                taskYIELD();
                continue;
            }
            xSemaphoreGive(rx_sem);
            break;
        }
    } else if (type == TX_LL) {
        while (1) {
            ESP_LOGI(TAG, "Adding packet to the TX_LL, currently has %d", ll_get_counter(TX_LL));
            rc = xSemaphoreTake(tx_sem, delay);
            if (rc != pdTRUE) {
                ASSERT(0);
            }
            if (len_tx_ll == MAX_TX_LEN) {
                xSemaphoreGive(tx_sem);
                taskYIELD();
                continue;
            }
            xSemaphoreGive(tx_sem);
            break;
        }
    } else {
        while (1) {
            ESP_LOGI(TAG, "Adding a packet to the CR_LL, currently has %d", ll_get_counter(CR_LL));
            rc = xSemaphoreTake(cr_sem, delay);
            if (rc != pdTRUE) {
                ASSERT(0);
            }
            if (len_cr_ll == MAX_CR_LEN) {
                xSemaphoreGive(cr_sem);
                taskYIELD();
                continue;
            }
            xSemaphoreGive(cr_sem);
            break;
        }
    }
}

// Returns -1 for error, transaction_id otherwise
int ll_add_node(ll_type_e type, void* data, size_t size, uint16_t transaction_id, bool store) {
    int ret;

    // Spin till we have room in the RX/TX/CR buffer
    // (assumes sigle producer of data)
    spin_till_free(type);

    if (type == RX_LL) {
        xSemaphoreTake(rx_sem, portMAX_DELAY);
        len_rx_ll++;
        ret = add_node(data, size, &root_rx, transaction_id, store);
        xSemaphoreGive(rx_sem);
        return ret;
    } else if (type == TX_LL) {
        xSemaphoreTake(tx_sem, portMAX_DELAY);
        len_tx_ll++;
        ret = add_node(data, size, &root_tx, transaction_id, store);
        xSemaphoreGive(tx_sem);
        return ret;
    } else {
        xSemaphoreTake(cr_sem, portMAX_DELAY);
        len_cr_ll++;
        ret = add_node(data, size, &root_cr, transaction_id, store);
        xSemaphoreGive(cr_sem);
        return ret;
    }
}

void ll_init() {
    rx_sem = xSemaphoreCreateMutex(); // Gaurds RX LL
    tx_sem = xSemaphoreCreateMutex(); // Guards TX LL
    cr_sem = xSemaphoreCreateMutex(); // Guards CORE LL

    //Assert if not enough memory to create objects
    ASSERT(rx_sem);
    ASSERT(tx_sem);
    ASSERT(cr_sem);
}

/**********************************************************
*                LL - CORE WALKING FUNCIONS
**********************************************************/

void take_ll_sem(const ll_type_e type) {
    if (type == TX_LL) {
        xSemaphoreTake(tx_sem, portMAX_DELAY);
    } else if (type == CR_LL) {
        xSemaphoreTake(cr_sem, portMAX_DELAY);
    }
}

void give_ll_sem(const ll_type_e type) {
    if (type == TX_LL) {
        xSemaphoreGive(tx_sem);
    } else if (type == CR_LL) {
        xSemaphoreGive(cr_sem);
    }
}

// Resets the LL peek function to point to the root
// Returns
//  - on an empty   LL ---> -1
//  - other cases       ---> 0
int ll_walk_reset(const ll_type_e type) {
    if (type == TX_LL) {
        if (root_tx == NULL) {
            return -1;
        }
        cur_node_tx = root_tx;
        return 0;
    } else if (type == CR_LL) {
        if (root_cr == NULL) {
            return -1;
        }
        cur_node_tx = root_cr;
        return 0;
    } else {
        assert(0);
        return 0; // to stop the compiler from whining
    }
}

// Returns the next node in the LL to be "looked at", increments
// an internal variable to point to the the next node to be looked at
// next time this function is called
node_t* walk_ll(const ll_type_e type) {
    if (type == TX_LL) {
        node_t* ret = cur_node_tx;
        if (cur_node_tx == NULL) {
            return NULL;
        }
        cur_node_tx = cur_node_tx->next;
        return ret;
    } else if (type == CR_LL) {
        node_t* ret = cur_node_cr;
        if (cur_node_cr == NULL) {
            return NULL;
        }
        cur_node_cr = cur_node_cr->next;
        return ret;
    } else {
        assert(0);
        return 0; // to stop the compiler from whining
    }
}

#if 1
void ll_test() {
    ESP_LOGI(TAG, "Starting LL test");

    int i = 0;
    for (; i < 5; i++) {
        int x = i;
        ll_add_node(CR_LL, &x, sizeof(int), i, true);
    }
    int test;
    ll_peek(3, TRUE, &test);

    ESP_LOGI(TAG, "peek = %d", test);
    print_debug(CR_LL);
}
#endif
