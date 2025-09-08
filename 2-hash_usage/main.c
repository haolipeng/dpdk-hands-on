/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_debug.h>
#include <rte_log.h>
#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_ip4.h>

/*
 * 5-tuple key type.
 * Should be packed to avoid holes with potentially
 * undefined content in the middle.
 */ 
struct flow_key {
	uint32_t ip_src; 	//src address
	uint32_t ip_dst;	//dst address
	uint16_t port_src;	//src port
	uint16_t port_dst;	//dst port
	uint8_t proto;		//protocol
}__rte_packed;

static void
init_test_flow_key(struct flow_key *key)
{
	key->ip_src = RTE_IPV4(0x03, 0x02, 0x01, 0x00);
	key->ip_dst = RTE_IPV4(0x07, 0x06, 0x05, 0x04);
	key->port_src = 0x0908;
	key->port_dst = 0x0b0a;
	key->proto = 15;
}

/* parameters for hash table */
struct rte_hash_parameters params = {
	.name = "flow_table",	//name of the hash table
	.entries = 64,		//number of entries in the hash table
	.key_len = sizeof(struct flow_key),	//length of the key
	.hash_func = rte_jhash,	//hash function
	.hash_func_init_val = 0,	//initial value for the hash function
	.socket_id = 0,		//socket id
};

/* Initialization of Environment Abstraction Layer (EAL). 8< */
int main(int argc, char **argv)
{
	int ret;
	struct rte_hash *hash_table = NULL;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		RTE_LOG(ERR, EAL, "Cannot init EAL: %s\n", strerror(-ret));
		goto cleanup;
	}

	/* create a key with 5 tuple */
	struct flow_key firstKey;
	init_test_flow_key(&firstKey);//fill the key with 5 tuple	

	/* create hash table */
	hash_table = rte_hash_create(&params);
	if (hash_table == NULL) {
		printf("ERROR: Cannot create hash table\n");
		goto cleanup;
	}

	/* add key to hash table */
	int pos = rte_hash_add_key(hash_table, &firstKey);
	if (pos < 0) {
		printf("ERROR: Cannot add key to hash table: %s\n", strerror(-pos));
		goto cleanup;
	}

	/* lookup key in hash table */
	pos = rte_hash_lookup(hash_table, &firstKey);
	if (pos < 0) {
		printf("ERROR: Cannot lookup key in hash table: %s\n", strerror(-pos));
		goto cleanup;
	}

	/* Simulate 5-tuple session lookup */
	struct flow_key secondKey;
	
	/* fill the key with same 5 tuple as firstKey */
	init_test_flow_key(&secondKey);

	pos = rte_hash_lookup(hash_table, &secondKey);
	if (pos < 0) {
		printf("ERROR: Cannot lookup session key in hash table: %s\n", strerror(-pos));
		/* calculate the hash of the session key */
		uint32_t hashNow = rte_jhash(&secondKey, sizeof(struct flow_key), 0);
		printf("INFO: Hash of session key: %u\n", hashNow);

		uint32_t hashBefore = rte_jhash(&firstKey, sizeof(struct flow_key), 0);
		printf("INFO: Hash of original key: %u\n", hashBefore);
		goto cleanup;
	}

	/* delete key from hash table */
	pos = rte_hash_del_key(hash_table, &firstKey);
	if (pos < 0) {
		printf("ERROR: Cannot delete key from hash table: %s\n", strerror(-pos));
		goto cleanup;
	}

	printf("INFO: Hash table operations completed successfully\n");

cleanup:
	/* clean up resources */
	if (hash_table != NULL) {
		rte_hash_free(hash_table);
		printf("INFO: Hash table successfully freed\n");
	}
	
	rte_eal_cleanup();
	RTE_LOG(INFO, EAL, "EAL cleanup completed\n");

	return (hash_table == NULL) ? -1 : 0;
}
