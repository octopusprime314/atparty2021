#pragma once

// These settings can be adjusted as needed
#define MAX_SRVS 128
#define NUM_SWAP_CHAIN_BUFFERS 3
#define CMD_LIST_NUM NUM_SWAP_CHAIN_BUFFERS
#define TIME_QUERIES_PER_CMD_LIST 8
#define TIME_QUERY_COUNT CMD_LIST_NUM* TIME_QUERIES_PER_CMD_LIST