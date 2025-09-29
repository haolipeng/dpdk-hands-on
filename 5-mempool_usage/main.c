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
#include <rte_mempool.h>

#define MEMPOOL_SIZE (1024)
#define MEMPOOL_ELT_SIZE (256)

//同名内存池，创建两次
static int
test_mempool_same_name_twice_creation(void)
{
	struct rte_mempool *mp_tc, *mp_tc2;
	char* mempool_name = "1234";

	//创建内存池
	mp_tc = rte_mempool_create(mempool_name, MEMPOOL_SIZE,
		MEMPOOL_ELT_SIZE, 0, 0,
		NULL, NULL,
		NULL, NULL,
		SOCKET_ID_ANY, 0);

	//内存池创建失败
	if (mp_tc == NULL)
	{
		printf("INFO: first mempool %s created failed\n", mempool_name);
		return -1;
	}

	mp_tc2 = rte_mempool_create(mempool_name, MEMPOOL_SIZE,
		MEMPOOL_ELT_SIZE, 0, 0,
		NULL, NULL,
		NULL, NULL,
		SOCKET_ID_ANY, 0);

	if (mp_tc2 == NULL)
	{
		printf("ERROR: second mempool %s created failed\n", mempool_name);
		return -1;
	}

	if (mp_tc2 != NULL) {
		rte_mempool_free(mp_tc);
		rte_mempool_free(mp_tc2);
		printf("INFO: mempool created successfully,let's free it\n");
	}

	rte_mempool_free(mp_tc);
	return 0;
}

static int
test_mempool_basic()
{
	uint32_t *objnum;
	void **objtable;
	void *obj, *obj2;
	char *obj_data;
	int ret = 0;
	unsigned i, j;
	int offset;

	//1.创建内存池
	printf("1.创建内存池\n");
	struct rte_mempool *mp = rte_mempool_create("test_mempool_basic", MEMPOOL_SIZE,
		MEMPOOL_ELT_SIZE, 0, 0,
		NULL, NULL,
		NULL, NULL,
		SOCKET_ID_ANY, 0);//重点看下这个函数

	if (mp == NULL)
	{
		printf("ERROR: create mempool failed\n");
		return -1;
	}
	/* 获取对象数量 */
	printf("可用对象数量: %d, 使用中对象数量: %d\n", rte_mempool_avail_count(mp), rte_mempool_in_use_count(mp));
	printf("-----------------------------------------\n");

	//2.获取一个对象
	printf("2.获取一个对象\n");
	if (rte_mempool_generic_get(mp, &obj, 1, NULL) < 0)
	{
		printf("ERROR: get object failed\n");
		return -1;
	}

	/* 获取对象数量 */
	printf("对象地址: %p,可用对象数量: %d, 使用中对象数量: %d\n", obj, rte_mempool_avail_count(mp), rte_mempool_in_use_count(mp));
	printf("-----------------------------------------\n");

	printf("3.将对象放回内存池\n");
	rte_mempool_generic_put(mp, &obj, 1, NULL);
	printf("对象地址: %p,可用对象数量: %d, 使用中对象数量: %d\n", obj, rte_mempool_avail_count(mp), rte_mempool_in_use_count(mp));
	printf("-----------------------------------------\n");

	printf("4.批量获取两个对象后获取对象数量\n");
	void *objects[2];
	if (rte_mempool_get_bulk(mp, objects, 2) < 0) //批量获取
	{
		printf("ERROR: bulk get objects failed\n");
		return -1;
	}
	obj = objects[0];
	obj2 = objects[1];
	printf("对象地址: %p, %p,可用对象数量: %d, 使用中对象数量: %d\n", 
		   obj, obj2, rte_mempool_avail_count(mp), rte_mempool_in_use_count(mp));
	printf("-----------------------------------------\n");

	printf("5.批量将两个对象归还给内存池\n");
	rte_mempool_put_bulk(mp, objects, 2); //批量归还
	printf("可用对象数量: %d, 使用中对象数量: %d\n", 
		   rte_mempool_avail_count(mp), rte_mempool_in_use_count(mp));
	printf("-----------------------------------------\n");

	printf("5.释放内存池\n");
	rte_mempool_free(mp);

	return ret;
}

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

	//测试同名的mempool能否创建两次
	ret = test_mempool_same_name_twice_creation();
	if (ret < 0) {
		printf("INFO: test_mempool_same_name_twice_creation testcase failed\n");
	}

	//测试mempool的常用api
	ret = test_mempool_basic();
	if (ret < 0) {
		printf("INFO: test_mempool_basic testcase failed\n");
	}

cleanup:
	rte_eal_cleanup();
	RTE_LOG(INFO, EAL, "EAL cleanup completed\n");

	return (hash_table == NULL) ? -1 : 0;
}
