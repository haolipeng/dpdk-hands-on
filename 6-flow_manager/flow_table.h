#ifndef _FLOW_TABLE_H_
#define _FLOW_TABLE_H_

#include <stdint.h>

#include <rte_hash.h>
#include <rte_jhash.h>

//初始化tcp会话表
int init_tcp_flow_table(void);

//处理tcp数据包，新建、更新、销毁会话
int process_tcp_session(uint32_t ipSrc, uint32_t ipDst, uint16_t portSrc, uint16_t portDst, uint8_t protocol, uint32_t pktLen);

//打印tcp会话表
void print_tcp_flow_table(void);

//销毁tcp会话表
int destroy_tcp_flow_table(void);

#endif