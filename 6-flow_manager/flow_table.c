#include "flow_table.h"
#include <stdio.h>
#include <stdlib.h>

struct flow_key{
    uint32_t ip_src;
    uint32_t ip_dst;
    uint16_t port_src;
    uint16_t port_dst;
    uint8_t proto;
}_rte_packed;

struct flow_value {
    uint64_t packets; //统计数据包数
    uint64_t bytes; //统计数据包字节数
};

/* parameters for hash table */
struct rte_hash_parameters params = {
	.name = "flow_table",	//name of the hash table
	.entries = 64,		//number of entries in the hash table
	.key_len = sizeof(struct flow_key),	//length of the key
	.hash_func = rte_jhash,	//hash function
	.hash_func_init_val = 0,	//initial value for the hash function
	.socket_id = 0,		//socket id
};

struct rte_hash *tcp_flow_table = NULL;

//初始化tcp会话表
int init_tcp_flow_table(void){
    tcp_flow_table = rte_hash_create(&params);
    if(NULL == tcp_flow_table){
        printf("tcp flow table create failed!\n");
        return -1;
    }
    return 0;
}

//处理tcp数据包，新建、更新、销毁会话
int process_tcp_session(uint32_t ipSrc, uint32_t ipDst, uint16_t portSrc, uint16_t portDst, uint8_t protocol, uint32_t pktLen){
    //0.变量定义
    int res = -1;
    void* FindData;

    //1.构造tcp会话表的flow_key
    struct flow_key key;
    key.ip_src = ipSrc < ipDst ? ipSrc : ipDst; //ipsrc 填写小的那个
    key.ip_dst = ipSrc < ipDst ? ipDst : ipSrc;//ipdst填写大的那个
    key.port_src = portSrc < portDst ? portSrc : portDst; //portsrc填写小的那个
    key.port_dst = portSrc < portDst ? portDst : portSrc; //portdst填写大的那个
    key.proto = protocol;

    uint32_t hash_value = rte_jhash(&key, sizeof(struct flow_key), 0);
    printf("hash_value: %u\n", hash_value);

    //2.使用flow_key去查找数据包对应的会话是否存在
    res = rte_hash_lookup_data(tcp_flow_table, &key, &FindData);
    if(res < 0){
        //2.2 会话不存在，创建新会话，并把bytes和packets进行正确赋值
        printf("session not exist,please create session!\n");

        //新增会话项
        struct flow_value* value = malloc(sizeof(struct flow_value));
        value->bytes = pktLen;
        value->packets = 1;

        res = rte_hash_add_key_data(tcp_flow_table, &key, value);
        if(res != 0){
            printf("rte_hash_add_key_data failed!\n");
            return -1;
        }
    }else{
        //2.1 会话已存在，则更新bytes和packets
        printf("session already exist!\n");
        
        //将FindData值类型转换struct flow_value类型
        struct flow_value* exitVal = (struct flow_value*)FindData;

        //更新会话项内容
        exitVal->bytes += pktLen;
        exitVal->packets += 1;
    }
    return 0;
}

//打印tcp会话表
void print_tcp_flow_table(void){
    const void *next_key;
    void *next_data;
    uint32_t iter = 0;
    while (rte_hash_iterate(tcp_flow_table, &next_key, &next_data, &iter) >= 0) {
        struct flow_key* key = (struct flow_key*) next_key;
        struct flow_value* value = (struct flow_value*) next_data;
        printf("ip_src: %d.%d.%d.%d, ip_dst: %d.%d.%d.%d, port_src: %u, port_dst: %u, proto: %u, bytes: %lu, packets: %lu\n", 
               (key->ip_src >> 24) & 0xFF, (key->ip_src >> 16) & 0xFF, (key->ip_src >> 8) & 0xFF, key->ip_src & 0xFF,
               (key->ip_dst >> 24) & 0xFF, (key->ip_dst >> 16) & 0xFF, (key->ip_dst >> 8) & 0xFF, key->ip_dst & 0xFF,
               key->port_src, key->port_dst, key->proto, value->bytes, value->packets);
    }

    return ;
}

//销毁tcp会话表
int destroy_tcp_flow_table(void){
    return 0;
}