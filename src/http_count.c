#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../../opt/MESA/include/MESA/stream.h"
#include "../../../opt/MESA/include/MESA/MESA_prof_load.h"
#include "../../../opt/MESA/include/MESA/MESA_handle_logger.h"
#include "../../../opt/MESA/include/MESA/http.h"

#define MAX_STR_LEN			256
#define PERSIST_HASH_SIZE	512
#define DOUBLE_DIRECTION	0x03
#define	MIN(a,b)			((a)<(b)?(a):(b))
#define HTTP_CHECK_CONFIG	"./plug/business/http_count/http_count.conf"
#define BASIC_DIR "./plug/business/http_count/"
#define HTTP_COUNT_PLUGNAME 		"http_count.so"
#define JHASH_GOLDEN_RATIO	0x9e3779b9
#define __jhash_mix(a, b, c)\
{\
	  a -= b; a -= c; a ^= (c>>13);\
	  b -= c; b -= a; b ^= (a<<8);\
	  c -= a; c -= b; c ^= (b>>13);\
	  a -= b; a -= c; a ^= (c>>12);\
	  b -= c; b -= a; b ^= (a<<16);\
	  c -= a; c -= b; c ^= (b>>5);\
	  a -= b; a -= c; a ^= (c>>3);\
	  b -= c; b -= a; b ^= (a<<10);\
	  c -= a; c -= b; c ^= (b>>15);\
}

int g_topn = 0, g_log_level = 0, g_type = 0;
char g_statistics_type[MAX_STR_LEN]="";
char g_http_count_log_path[MAX_STR_LEN]="";
void *g_http_count_log_handler = NULL;
struct http_data_htable *g_http_data_htable = NULL;
enum statistics_type
{
	HTTP_TYPE,
	USER_AGENT_TYPE,
	TYPE_ERROR,
};
	
struct http_count_node
{
	struct http_count_node *next;
	char data[MAX_STR_LEN];
	uint64_t count;//统计命中次数
};

struct http_data_htable
{
	struct http_count_node *hlist_head;
	uint64_t sum;//所有hash链总节点数统计
};

int add_hlist_node(uint32_t key, char *buf, uint32_t buflen)
{
	struct http_count_node *node = NULL;

	node = malloc(sizeof(struct http_count_node));
	if(!node)
	{
		printf("http_check.so: malloc node failed\n");
		MESA_handle_runtime_log(g_http_count_log_handler, RLOG_LV_FATAL, HTTP_COUNT_PLUGNAME, "malloc node failed!");
		return -1;
	}

	memset(node, 0, sizeof(struct http_count_node));
	strncpy(node->data, buf, buflen);
	node->count++;//命中次数
	node->next = g_http_data_htable->hlist_head[key].next;
	g_http_data_htable->hlist_head[key].next = node;
	g_http_data_htable->hlist_head[key].count++;//哈希链节点数
	g_http_data_htable->sum++;//所有hash链总节点数
	
	return 0;
}

struct http_count_node * find_hlist_node(uint32_t key, char *buf, uint32_t buflen)
{
	struct http_count_node *node = NULL;
	
	node = g_http_data_htable->hlist_head[key].next;
	for(; node != NULL; node = node->next)
	{
		if(!strncmp(node->data, buf, buflen))
		{
			return node;
		}
			
	}
	
	return NULL;
}

uint32_t jhash (void *key, uint32_t length, uint32_t initval)
{
	uint32_t a, b, c, len;
  	uint8_t *k = key;

  	len = length;
  	a = b = JHASH_GOLDEN_RATIO;
  	c = initval;

 	while (len >= 12)
    {
    	a += (k[0] + ((uint32_t) k[1] << 8) + ((uint32_t) k[2] << 16) + ((uint32_t) k[3] << 24));
      	b += (k[4] + ((uint32_t) k[5] << 8) + ((uint32_t) k[6] << 16) + ((uint32_t) k[7] << 24));
      	c += (k[8] + ((uint32_t) k[9] << 8) + ((uint32_t) k[10] << 16) + ((uint32_t) k[11] << 24));
      	__jhash_mix (a, b, c);
      	k += 12;
      	len -= 12;
    }

  	c += length;
  	switch (len)
    {
	    case 11:
	    	c += ((uint32_t) k[10] << 24);
	    case 10:
	      	c += ((uint32_t) k[9] << 16);
	    case 9:
	      	c += ((uint32_t) k[8] << 8);
	    case 8:
	      	b += ((uint32_t) k[7] << 24);
	    case 7:
	      	b += ((uint32_t) k[6] << 16);
	    case 6:
	      	b += ((uint32_t) k[5] << 8);
	    case 5:
	      	b += k[4];
	    case 4:
	      	a += ((uint32_t) k[3] << 24);
	    case 3:
	      	a += ((uint32_t) k[2] << 16);
	    case 2:
	      	a += ((uint32_t) k[1] << 8);
	    case 1:
	      	a += k[0];
    };

  	__jhash_mix (a, b, c);

  	return c;
}

uint32_t get_hash_key(void *buf, uint32_t buflen)
{
	return (jhash(buf, buflen, 0) % PERSIST_HASH_SIZE);
}

int http_count_statistics(char *buf, uint32_t len)
{
	uint32_t key = 0;
	uint32_t buflen = 0;
	struct http_count_node *node = NULL;
	
	buflen = MIN(len, (MAX_STR_LEN-1));
	key = get_hash_key(buf, buflen);
	node = find_hlist_node(key, buf, buflen);
	if(node)
	{
		node->count++;
	}
	else
	{
		add_hlist_node(key, buf, buflen);
	}

	return 0;
}

char http_count_entry(stSessionInfo* session_info, void **param, int thread_seq, struct streaminfo *a_tcp, void *a_packet)
{	
	if(a_tcp->dir != DOUBLE_DIRECTION)
		return 0;
	
    switch(session_info->prot_flag)
    {
    	case HTTP_HOST:
			if(g_type == HTTP_TYPE)
				http_count_statistics(session_info->buf, session_info->buflen);
            break;
        case HTTP_USER_AGENT:
			if(g_type == USER_AGENT_TYPE)
          		http_count_statistics(session_info->buf, session_info->buflen);
            break;
        default:
            break;
    }
	
	return PROT_STATE_GIVEME;
}

int http_check_readconf(const char* filename)
{
	MESA_load_profile_int_def(filename, "HTTP", "TOPN", (int *)(&g_topn), 5);
    MESA_load_profile_string_nodef(filename, "HTTP", "TYPE", g_statistics_type, MAX_STR_LEN);
	MESA_load_profile_int_def(filename, "HTTP", "LOG_LEVEL", (int *)(&g_log_level), 30);
    MESA_load_profile_string_nodef(filename, "HTTP", "HTTP_COUNT_LOG_PATH", g_http_count_log_path, MAX_STR_LEN);

	g_http_count_log_handler = MESA_create_runtime_log_handle(g_http_count_log_path, g_log_level);
	if(!g_http_count_log_handler)
	{
		printf("http_check.so: get log handle error!\n");
		MESA_handle_runtime_log(g_http_count_log_handler, RLOG_LV_FATAL, HTTP_COUNT_PLUGNAME, "get log handle error!");	
		return -1;
	}
	if(!strncmp(g_statistics_type, "Host", 4))
		g_type = HTTP_TYPE;
	else if(!strncmp(g_statistics_type, "User-Agent", 10))
		g_type = USER_AGENT_TYPE;
	else
	{
		g_type = TYPE_ERROR;
		printf("http_check.so: get statistics type error!\n");
		MESA_handle_runtime_log(g_http_count_log_handler, RLOG_LV_FATAL, HTTP_COUNT_PLUGNAME, "get statistics type error!");	
		return -1;
	}
    //printf("***v1***conf:topn:%d, key:%s(%d), log_level:%d, log_path:%s\n",g_topn, g_statistics_type, g_type, g_log_level, g_http_count_log_path);
	MESA_handle_runtime_log(g_http_count_log_handler, RLOG_LV_DEBUG, HTTP_COUNT_PLUGNAME, 
		"read_conf:topn:%d, key:%s(%d), log_level:%d, log_path:%s\n", g_topn, g_statistics_type, g_type, g_log_level, g_http_count_log_path);

	return 0;
}

int http_htable_init(void)
{
	uint32_t i = 0;
	
	g_http_data_htable = malloc(sizeof(struct http_data_htable));
	if(!g_http_data_htable)
	{
		printf("http_check.so: malloc g_http_data_htable failed!\n");
		MESA_handle_runtime_log(g_http_count_log_handler, RLOG_LV_FATAL, HTTP_COUNT_PLUGNAME, "malloc g_http_data_htable failed!");	
		return -1;
	}
	memset(g_http_data_htable, 0, sizeof(struct http_data_htable));
	g_http_data_htable->hlist_head = malloc(sizeof(struct http_count_node) * PERSIST_HASH_SIZE);
	if(!g_http_data_htable->hlist_head)
	{
		printf("http_check.so: malloc g_http_data_htable->hlist_head failed!\n");
		MESA_handle_runtime_log(g_http_count_log_handler, RLOG_LV_FATAL, HTTP_COUNT_PLUGNAME, "malloc g_http_data_htable->hlist_head failed!");	
		return -1;
	}
	for(i=0; i < PERSIST_HASH_SIZE; i++)
		memset(&g_http_data_htable->hlist_head[i], 0, sizeof(struct http_count_node));

	return 0;
}

int http_count_init(void)
{
	int ret = 0;
	
	ret = http_check_readconf(HTTP_CHECK_CONFIG);
	if(ret)
	{
		goto OUT;
	}
	ret = http_htable_init();
	if(ret)
	{
		goto OUT;
	}

OUT:
	return ret;
}

void free_all(void)
{
	uint32_t i = 0;
	struct http_count_node *free_node = NULL;
	struct http_count_node *head = NULL;
	
	for(i = 0; i < PERSIST_HASH_SIZE; i++)
	{
		head = &g_http_data_htable->hlist_head[i];
		free_node = head->next;
		for(; free_node != NULL; free_node = head->next)
		{
			head->next = free_node->next;
			free_node->next = NULL;
			free(free_node);
			free_node = NULL;
		}
	}
	free(g_http_data_htable->hlist_head);
	g_http_data_htable->hlist_head = NULL;
	free(g_http_data_htable);
	g_http_data_htable = NULL;

	return;
}

void http_count_destory(void)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint64_t max_count;
	uint8_t flag = 0;
	struct http_count_node *node = NULL;
	struct http_count_node *max_node = NULL;
	char buff[MAX_STR_LEN] = "";
	FILE *fp = NULL;

	sprintf(buff, "%s%s.txt", BASIC_DIR, g_statistics_type);
	fp = fopen(buff, "w+");
	if(!fp)
	{
		printf("http_check.so: open file error!\n");
		MESA_handle_runtime_log(g_http_count_log_handler, RLOG_LV_FATAL, HTTP_COUNT_PLUGNAME, "open file %s error!", buff);	
		goto OUT;
	}
	
	if(g_type == HTTP_TYPE)
		fprintf(fp, "%-100s	%s\n","Host", "Count");
	else if(g_type == USER_AGENT_TYPE)
		fprintf(fp, "%-180s	%s\n","User-Agent", "Count");
#if 1
	for(i = 0; i < PERSIST_HASH_SIZE; i++)//print_test
	{
		node = g_http_data_htable->hlist_head[i].next;
		for(; node != NULL; node = node->next)
		{
			printf("hash_print:key:%d, data:%s, count:%"PRIu64", hash_node:%"PRIu64", all_node:%"PRIu64"\n",
				i, node->data, node->count, g_http_data_htable->hlist_head[i].count, g_http_data_htable->sum);	
		}
	}
#endif

	printf("***************************top%d***************************\n", g_topn);
	for(j = 0; j < g_topn; j++)
	{
		flag = 0;
		for(i = 0; i < PERSIST_HASH_SIZE; i++)
		{
			node = g_http_data_htable->hlist_head[i].next;
			for(; node != NULL; node = node->next)
			{
				if (!flag)
				{
					max_count = node->count;
					max_node = node;
					flag = 1;
				}
				if(max_count < node->count)
				{
					max_count = node->count;
					max_node = node;
				}
			}	
		}
		if(max_node && max_node->count)
		{
			printf("top%d: %s, count:%"PRIu64"\n",g_topn, max_node->data, max_node->count);
			MESA_handle_runtime_log(g_http_count_log_handler, RLOG_LV_DEBUG, HTTP_COUNT_PLUGNAME, 
				"top%d: %s, count:%d\n", g_topn, max_node->data, max_node->count);
			if(g_type == HTTP_TYPE)
				fprintf(fp, "%-100s	%"PRIu64"\n",  max_node->data, max_node->count);
			else if(g_type == USER_AGENT_TYPE)
				fprintf(fp, "%-180s	%"PRIu64"\n",  max_node->data, max_node->count);
			max_node->count = 0;
		}
	}

OUT:
	if(fp)
		fclose(fp);
	if(g_http_count_log_handler)
		MESA_destroy_runtime_log_handle(g_http_count_log_handler);
	free_all();
		
    return ;
}
