#include <stdio.h>
#include "ubt_rpc.h"

#define RPC_LOG_D(...)  (rpc_printf("[RPC] ");rpc_printf(__VA_ARGS__);rpc_printf("\r\n");)

static void ubt_rpc_impl_lock(ubt_rpc_t *rpc)
{
    const osMutexAttr_t mutex_attr = {
        .name = NULL,
        .attr_bits = osMutexRecursive,
        .cb_mem = NULL,
        .cb_size = 0
    };
    if (rpc->mutex == NULL) {
        rpc->mutex = osMutexNew(&mutex_attr);
        if(rpc->mutex == NULL){
            RPC_LOG_D("mutex create fail!");
        }
    }
    osMutexAcquire(rpc->mutex, osWaitForever);
}

static void ubt_rpc_impl_unlock(ubt_rpc_t *rpc)
{
    osMutexRelease(rpc->mutex);
}

static uint32_t gen_request_id(ubt_rpc_t *rpc)
{
    uint32_t id;
    ubt_rpc_impl_lock(rpc);
    id = rpc->call_id++;
    ubt_rpc_impl_unlock(rpc);
    return id;
}

static osMessageQueueId_t *get_queue_for_request(ubt_rpc_t *rpc)
{
    osMessageQueueId_t *idle_queue = NULL;
    ubt_rpc_impl_lock(rpc);
    if (rpc->active_request < rpc->max_request) {
        idle_queue = osMessageQueueNew(1, sizeof(rpc_message_t *), NULL);
        if (idle_queue) {
            rpc->active_request++;
        } else {
            RPC_LOG_D("request queue create fail!");
        }
    } else {
        RPC_LOG_D("request queue cnt is max!");
    }
    ubt_rpc_impl_unlock(rpc);
    return idle_queue;
}

static ubt_rpc_request_t *ubt_rpc_create_request(ubt_rpc_t *rpc, bool need_ack)
{
    ubt_rpc_request_t *request = NULL;
    do {
        request = (ubt_rpc_request_t *)rpc_malloc(sizeof(ubt_rpc_request_t));
		RPC_LOG_D("%s %d alloc %p", __FUNCTION__, __LINE__, request);
        if (request == NULL) {
            break;
        }
        memset(request, 0, sizeof(ubt_rpc_request_t));
        request->data_buf = rpc_malloc(rpc->buffer_size);
		RPC_LOG_D("%s %d alloc %p", __FUNCTION__, __LINE__, request->data_buf);
        request->data_len = rpc->buffer_size;
        if (!request->data_buf) {
            rpc_free(request);
            request = NULL;
            break;
        }

        if(need_ack){
            request->queue = get_queue_for_request(rpc);
            if (!request->queue) {
                rpc_free(request->data_buf);
                rpc_free(request);
                request = NULL;
                break;
            }
        }
        list_init(&request->list);
    } while (0);
    RPC_LOG_D("ubt_rpc_create_request %p", request);
    return request;
}

static void ubt_rpc_message_free(rpc_message_t *message)
{
    RPC_LOG_D("ubt_rpc_message_free");
    if (message->struct_data) {
        rpc_free(message->struct_data);
        message->struct_data = NULL;
    }
    rpc_free(message);
}

#ifdef RPC_TX_STANDALONE_THREAD
static void ubt_rpc_handle_input_message(rpc_message_t *message)
{
    if (message == NULL) {
        return;
    }

    if (MSG_IS_NOTIFY(message)) {
        if (message->rpc->notify_handler) {
            message->rpc->notify_handler(message);
        }
    } else {
        if (message->rpc->request_handler) {
            void *rv = message->rpc->request_handler(message);
            if (rv) {
                //uint8_t attr = message->attr == Header_Attr_REQUEST ? Header_Attr_REQACK : Header_Attr_PUBACK;
                //ubt_rpc_perform_ack(message->rpc, message->dev, message->cmd, message->id, message->seq, attr, message->ack_code, rv);
                rpc_request_config_t req_conf = {
                    .retry = 0,
                    .err = 0,
                    .ctrl = ATTR_REQ_ACK,
                    .expect_ack = false,
                    .cmd = message->cmd + 1,
                    .timeout = 0,
                    .seq = message->seq,
                };
                ubt_rpc_perform(message->rpc, &req_conf, rv);
            }
        }
    }
    ubt_rpc_message_free(message);
}
#else 
static void ubt_rpc_handle_input_message(threadpool_t *pool, void *context)
{

#error user's impl

}
#endif

static int ubt_rpc_dispatch(ubt_rpc_t *rpc, rpc_message_t *message)
{
	RPC_LOG_D("freesize1:%d", rpc_get_freeheap_size());
    ubt_rpc_request_t *iter, *tmp;
    if (!MSG_IS_ACK(message)) {
#if RPC_TX_STANDALONE_THREAD
		ubt_rpc_handle_input_message(message);
#else

#error user's impl  ubt_rpc_handle_input_message()

#endif
    } else if (MSG_IS_ACK(message)) {
        ubt_rpc_impl_lock(rpc);
        if (!list_empty(&rpc->wait_response_head)) {
            list_for_each_entry_safe(iter, tmp, &rpc->wait_response_head, list) {
                if (iter->request_id == message->seq) {
                    if (osMessageQueuePut(iter->queue, &message, 0, 0) == osOK) {
                    }
                    break;
                }
            }   
        }
        ubt_rpc_impl_unlock(rpc);
    } else {
        RPC_LOG_D("message type is not support");
        rpc_assert(0);
    }
	RPC_LOG_D("freesize2:%d", rpc_get_freeheap_size());
    return 0;
}

static void message_callback(void *pb_codec)
{
	// ubt_rpc_codec_t *codec = (ubt_rpc_codec_t*)pb_codec;
	// rpc_message_t  *message = (rpc_message_t *)rpc_malloc(sizeof(rpc_message_t));
	// RPC_LOG_D("%s alloc %p", __FUNCTION__, message);
    // if (message) {
    //     memset(message, 0, sizeof(rpc_message_t));
    //     ubt_rpc_t *rpc = (ubt_rpc_t *)codec->rpc_context;
    //     message->cmd = codec->msg.cmd;
    //     message->ctrl= codec->msg.ctrl;
    //     message->err = codec->msg.err;
    //     message->seq = codec->msg.seq;
    //     message->rpc = rpc;
    //     RPC_LOG_D("receive message ctrl:%d, cmd:%d, seq:%d, err:%d", message->ctrl, message->cmd, message->seq, message->err);
    //     message->struct_data = rpc_message_unserialize(&codec->msg);
    //     if (message->struct_data) {
    //         if (ubt_rpc_dispatch(rpc, message) != 0) {
    //             RPC_LOG_D("msg dispatch failed");
    //             ubt_rpc_message_free(message);
    //         }
    //     } else {
    //         RPC_LOG_D("struct data == NULL");
    //         ubt_rpc_message_free(message);
    //     }
    // }
}

static void ubt_rpc_request_destroy(ubt_rpc_t *rpc, ubt_rpc_request_t *request)
{
    RPC_LOG_D("ubt_rpc_request_destroy %p", request);
    ubt_rpc_impl_lock(rpc);
    list_del(&request->list);
    if (rpc->active_request) {
        rpc->active_request--;
    }
    ubt_rpc_impl_unlock(rpc);
    if(request->queue){
        osMessageQueueDelete(request->queue);
    }
    if (request->data_buf) {
        rpc_free(request->data_buf);
    }
    rpc_free(request);
}

static void ubt_rpc_process_output(ubt_rpc_t *rpc)
// {
//     ubt_rpc_request_t *iter, *tmp;
//     ubt_rpc_impl_lock(rpc);
//     if (!list_empty(&rpc->call_list_head)) {
//         list_for_each_entry_safe(iter, tmp, &rpc->call_list_head, list) {
//             rpc->pb_codec->transport.write(iter->data_buf, iter->data_len, iter->mask);
//             if (!iter->expect_ack) {
//                 ubt_rpc_request_destroy(rpc, iter);
//             } else {
//                 list_del(&iter->list);
//                 list_add_tail(&iter->list, &rpc->wait_response_head);
//             }
//         }
//     }
//     ubt_rpc_impl_unlock(rpc);
// }

static void rpc_runner(void *arg)
{
    ubt_rpc_t *rpc = (ubt_rpc_t *)arg;
    RPC_LOG_D("rpc runner started");
    for (;;) {
        // if (rpc->pb_codec->transport.wait_data(osWaitForever) == osOK) {
        //     ubt_rpc_codec_process(rpc->pb_codec);
#ifndef RPC_TX_STANDALONE_THREAD
            // ubt_rpc_process_output(rpc);
#endif
        // }
    }
}

#ifdef RPC_TX_STANDALONE_THREAD
static void rpc_tx_runner(void *arg)
{
    ubt_rpc_t *rpc = (ubt_rpc_t *)arg;
    RPC_LOG_D("rpc tx runner started");
    for (;;) {
        if (osSemaphoreAcquire(rpc->tx_sem, osWaitForever) == osOK) {
            ubt_rpc_process_output(rpc);
        }
    }
}
#endif

ubt_rpc_t *ubt_rpc_create(ubt_rpc_config_t *config)
{
    rpc_assert(config);
    rpc_assert(config->task_stack_size < 8192);
    uint32_t stack_size = config->task_stack_size;
    uint32_t buffer_size = config->buffer_size;
    ubt_rpc_t *rpc = (ubt_rpc_t *)rpc_malloc(sizeof(ubt_rpc_t));
    if (!rpc) {
        return NULL;
    }
    memset(rpc, 0, sizeof(ubt_rpc_t));
    rpc->poll_sem = osSemaphoreNew(0xFFFFFFFF, 0, NULL);
    list_init(&rpc->call_list_head);
    list_init(&rpc->wait_response_head);
    if (config->max_request == 0 || config->max_request > RPC_MAX_CONCURRENT) {
        rpc->max_request = RPC_MAX_CONCURRENT;
    } else {
        rpc->max_request = config->max_request;
    }

    if (stack_size == 0) {
        stack_size = 4096;
    }
    if (0 == buffer_size) {
        buffer_size = 256;
    }
    rpc->buffer_size = buffer_size;
    rpc->notify_handler = config->notify_handler;
    rpc->request_handler = config->request_handler;
    // rpc->pb_codec = ubt_rpc_codec_create((void *)rpc);
    // if (!rpc->pb_codec) {
    //     rpc_free(rpc);
    //     return NULL;
    // }

    // ubt_rpc_codec_set_transport(rpc->pb_codec, &config->transport);
    // ubt_rpc_codec_set_on_message_callback(rpc->pb_codec, message_callback);

    const osThreadAttr_t thread_attr = {
        .name = "rx",
        .attr_bits = 0,
        .cb_mem = NULL,
        .cb_size = 0,
        .stack_mem = NULL,
        .stack_size = stack_size,
        .priority = osPriorityBelowNormal1,
        .tz_module = 0,
        .reserved = 0
    };

    rpc->thread_id =  osThreadNew(rpc_runner, rpc, &thread_attr);
    if (!rpc->thread_id) {
        ubt_rpc_codec_destroy(rpc->pb_codec);
        rpc_free(rpc);
        rpc = NULL;
    }
#if RPC_TX_STANDALONE_THREAD
    const osThreadAttr_t thread_tx_attr = {
        .name = "tx",
        .attr_bits = 0,
        .cb_mem = NULL,
        .cb_size = 0,
        .stack_mem = NULL,
        .stack_size = stack_size,
        .priority = osPriorityBelowNormal,
        .tz_module = 0,
        .reserved = 0
    };	
	rpc->tx_thread = osThreadNew(rpc_tx_runner, rpc, &thread_tx_attr);
    if (!rpc->tx_thread) {
		osThreadTerminate(rpc->thread_id);
        ubt_rpc_codec_destroy(rpc->pb_codec);
        rpc_free(rpc);
        rpc = NULL;
    }
	rpc->tx_sem = osSemaphoreNew(0xFFFFFFFF, 0, NULL);
#endif
    return rpc;
}

void ubt_rpc_destroy(ubt_rpc_t *rpc)
{
    osMutexDelete(rpc->mutex);
    // ubt_rpc_codec_destroy(rpc->pb_codec);
    osThreadTerminate(rpc->thread_id);
    rpc_free(rpc);
}

// static ubt_err_t ubt_rpc_encode_header(ubt_rpc_t *rpc, ubt_rpc_request_t *request, rpc_request_config_t *req_conf, common_message *pkt)
// {
//     uint32_t frame_len = 0;

//     if(request->data_len > sizeof(pkt->body.bytes)){
//         return UBT_ERR_NO_MEM;
//     }

//     //memcpy(pkt->body.bytes, request->data_buf, request->data_len);
//     //pkt->body.size = request->data_len;

//     //header encode
//     pb_ostream_t stream = pb_ostream_from_buffer ( request->data_buf + 2, request->data_len - 3);
//     if ( !pb_encode ( &stream, common_message_fields, pkt ) ) {
//         ubt_log_error(TAG, "pb encode common_message_fields failed\n");
//         return UBT_ERR_PARAM;
//     }

//     if (stream.bytes_written == 0) {
//         ubt_log_error(TAG, "payload len == 0\n");
//         return UBT_ERR_NO_MEM;
//     }

//     request->data_buf[frame_len++] = FRAME_HEAD;
//     request->data_buf[frame_len++] = stream.bytes_written;
// 	frame_len += stream.bytes_written;
//     request->data_buf[frame_len++] = crc8 ( request->data_buf + 2, stream.bytes_written );

//     request->data_len = stream.bytes_written + 3;

//     return UBT_ERR_SUCCESS;
// }

// static ubt_err_t ubt_rpc_encode_data(common_message *msgbuf, void *param)
// {
//     return rpc_message_serialize(msgbuf, param);
// }

static int ubt_rpc_output_enqueue(ubt_rpc_t *rpc, ubt_rpc_request_t *request)
{
    ubt_rpc_impl_lock(rpc);
    list_add_tail(&request->list, &rpc->call_list_head);
    ubt_rpc_impl_unlock(rpc);
#if RPC_TX_STANDALONE_THREAD
    osSemaphoreRelease(rpc->tx_sem);
#else
	osSemaphoreRelease(rpc->poll_sem);
#endif

    return 0;
}

void ubt_rpc_rx_data_notify(ubt_rpc_t *rpc)
{
    if (rpc && rpc->poll_sem) {
        osSemaphoreRelease(rpc->poll_sem);
    }
}

static void *ubt_rpc_wait_response(ubt_rpc_t *rpc, ubt_rpc_request_t *request)
{
    rpc_message_t *message = NULL;
    void *rv = NULL;
    if (osMessageQueueGet(request->queue, &message, NULL, UBT_RPC_DEFAULT_WAIT_TIMEOUT / portTICK_PERIOD_MS) == osOK) {
        if (message->cmd != request->cmd) {
            RPC_LOG_D("response no match, req_cmd=%d, rcv_cmd=%d", request->cmd, message->cmd);
            ubt_rpc_message_free(message);
        } else {
            rv = message->struct_data;
            RPC_LOG_D("response match, req_cmd=%d", message->cmd);
            rpc_free(message);
        }
    } else {
        RPC_LOG_D("cmd %d wait respone timeout\n", request->cmd);
    }
    return rv;
}

void *ubt_rpc_perform(ubt_rpc_t *rpc, rpc_request_config_t *req_conf, void *param)
{
    void *response = NULL;
    ubt_rpc_request_t *req = NULL;
    int err = -1;

	if(rpc == NULL){
		RPC_LOG_D("rpc no init");
		return NULL;
	}
    if(req_conf == NULL) {
        RPC_LOG_D("req_conf==NULL");
    }
    do {
        req = ubt_rpc_create_request(rpc, req_conf->expect_ack);
        if (!req) {
            RPC_LOG_D("rpc req create fail");
            break;
        }
        if(MSG_IS_ACK(req_conf)){
            req->base.seq = req_conf->seq;
        }else{
            req->base.seq = gen_request_id(rpc);
        }
        req->base.cmd = req_conf->cmd;        
#if RPC_ADDRESS_SUPPORT
        req->base.src = req_conf->src; 
        req->base.dest = req_conf->dst;
#endif 
        req->base.ctrl = req_conf->ctrl; 
        req->base.err = req_conf->err;
        req->expect_ack = req_conf->expect_ack;
        req->retry = req_conf->retry;
        req->timeout = req_conf->timeout;
        req->param = param;
        
        RPC_LOG_D("perform request cmd:%d,seq:%d", req_conf->base.cmd, req->base.seq);

        ubt_rpc_output_cmd(rpc, req);

        if(req_conf->expect_ack){
            response = ubt_rpc_wait_response(rpc, req);
        }
        err = 0;
    } while (0);

    if(MSG_IS_ACK(req_conf)){
        if(param) rpc_free(param);
    }

    if(req){
        if(req_conf->expect_ack){
            ubt_rpc_request_destroy (rpc, req );
        }else if (err != 0) {
            ubt_rpc_request_destroy (rpc, req );
        }
    }
	if(pkt != NULL){
		rpc_free(pkt);
	}
    return response;
}
