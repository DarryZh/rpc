#ifndef __UBT_RPC_CONFIG_H__
#define __UBT_RPC_CONFIG_H__

#define rpc_printf(...)             printf(__VA_ARGS__)

#define rpc_assert                  assert

#define rpc_malloc                  malloc

#define rpc_free                    free

#define rpc_get_freeheap_size       xPortGetFreeHeapSize

#define rpc_get_system_ms           

#define RPC_TX_STANDALONE_THREAD

#define RPC_ADDRESS_SUPPORT
#endif