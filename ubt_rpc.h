#ifndef __UBT_RPC_H__
#define __UBT_RPC_H__
#include "ubt_rpc_config.h"
#include "ubt_rpc_list.h"
#include "cmsis_os2.h"

#define UBT_RPC_DEFAULT_WAIT_TIMEOUT 5000
#define RPC_MAX_CONCURRENT 6

typedef struct {
#ifdef RPC_ADDRESS_SUPPORT
    uint8_t src;
    uint8_t dst;
#endif
    uint8_t ctrl;       // msg type
    uint8_t err;
    uint32_t seq;       // 序号
    uint32_t cmd;       // index
} ubt_rpc_msg_base_t;

typedef struct {
    struct list_head list;
    osMessageQueueId_t *queue;

    ubt_rpc_msg_base_t base;

    uint16_t retry;
    bool expect_ack;
    uint32_t timeout;

    void *param;
} ubt_rpc_request_t;

struct ubt_rpc;
typedef struct ubt_rpc ubt_rpc_t;
typedef struct {
    ubt_rpc_msg_base_t base;

    void *struct_data;
    ubt_rpc_t *rpc;
} rpc_message_t;

#define ATTR_REQ        0
#define ATTR_RSP        1
#define ATTR_NOTIFY     2
#define ATTR_REQ_ACK    3
#define ATTR_RSP_ACK    4
#define ATTR_NOTI_ACK   5
#define ATTR_RSP_REQ    6

#define MSG_IS_REQ(msg) (((msg)->ctrl&0x1F) == ATTR_REQ)
#define MSG_IS_ACK(msg) ((((msg)->ctrl&0x1F) == ATTR_REQ_ACK) || (((msg)->ctrl&0x1F) == ATTR_RSP_ACK) || (((msg)->ctrl&0x1F) == ATTR_NOTI_ACK))
#define MSG_IS_NOTIFY(msg) (((msg)->ctrl&0x1F) == ATTR_NOTIFY)

#define MSG_IS_OUTDIR(msg) ((((msg)->ctrl&0x1F) == ATTR_REQ) || (((msg)->ctrl&0x1F) == ATTR_NOTIFY) || (((msg)->ctrl&0x1F) == ATTR_RSP_REQ))

typedef struct{
    ubt_rpc_msg_base_t base;

    uint16_t retry;
    bool expect_ack;
    uint32_t timeout;
}rpc_request_config_t;

struct ubt_rpc {
    struct list_head call_list_head;
    struct list_head wait_response_head;

    osMutexId_t mutex;
    osSemaphoreId_t poll_sem;

    char *name;
    uint32_t call_id;
    osThreadId_t thread_id;
    
    uint16_t active_request;
    uint16_t max_request;
};

typedef struct {
    char *name;
    uint32_t task_stack_size;
    uint16_t max_request;
} ubt_rpc_config_t;

ubt_rpc_t *ubt_rpc_create(ubt_rpc_config_t *config);
void ubt_rpc_destroy(ubt_rpc_t *rpc);
void *ubt_rpc_perform_request(ubt_rpc_t *rpc, uint32_t dst_dev, uint32_t id, uint32_t cmd, void *param);
void ubt_rpc_perform_push(ubt_rpc_t *rpc, uint32_t dst_dev, uint32_t id, uint32_t cmd, void *param, uint32_t mask);
void *ubt_rpc_perform(ubt_rpc_t *rpc, rpc_request_config_t *req_conf, void *param);
void ubt_rpc_output_cmd(ubt_rpc_t *rpc, ubt_rpc_request_t req);

#endif
