#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

long long int* prevUnusedMem = NULL;
long long int* prevUsedMem = NULL;
long long int firstVcupu = 0;

int is_exit = 0; // DO NOT MODIFY THE VARIABLE

void MemoryScheduler(virConnectPtr conn,int interval);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if(argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);
	
	conn = virConnectOpen("qemu:///system");
	if(conn == NULL)
	{
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	virConnectClose(conn);
	free( prevUnusedMem );
	free( prevUsedMem );
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval)
{
	virDomainPtr *domains;
	virDomainInfoPtr info = malloc( sizeof( virDomainInfo ) );
	virDomainMemoryStatPtr memInfo= malloc( 14 * sizeof( virDomainMemoryStatStruct ) );

	int numDomains = virConnectListAllDomains( conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING );

	if( prevUnusedMem == NULL )
	{
		prevUnusedMem = calloc( numDomains, sizeof( long long int ) );
		prevUsedMem = calloc( numDomains, sizeof( long long int ) );
	}

	long long int* ballonSizes = calloc( numDomains, sizeof( long long int ) );
	long long int* memRequired = calloc( numDomains, sizeof( long long int ) );
	long long int* memSurplus = calloc( numDomains, sizeof( long long int ) );
	int allVmMaxed = 1;

	for( size_t i = 0; i < numDomains; i++)
	{
		virDomainSetMemoryStatsPeriod( domains[i], interval, VIR_DOMAIN_AFFECT_LIVE );
		virDomainGetInfo( domains[i], info );
		virDomainMemoryStats( domains[i], memInfo, 14, 0 );

		long long int ballonSize, unUsedMem, usedMem;
		for( size_t j = 0; j < 14; j++ )
		{
			if( memInfo[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON )
				ballonSize = memInfo[j].val / 1024;		

			if( memInfo[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED )
				unUsedMem = memInfo[j].val / 1024;				
		}

		usedMem = ballonSize - unUsedMem;

		if( prevUnusedMem[i] != 0 )
		{
			// A threshold of 10 mb to clear out noise
			if( usedMem > prevUsedMem[i] + 10 && unUsedMem < 200  )
				memRequired[i] = MIN( MAX( 200 - unUsedMem, 50 ), info->maxMem / 1024 - ballonSize );

			else if( usedMem <= prevUsedMem[i] && unUsedMem > 100 )
			{
				memSurplus[i] = MIN( 50, unUsedMem - 100 );
				allVmMaxed = 0;
			}
		}

		ballonSizes[i] = ballonSize;
		prevUnusedMem[i] = unUsedMem;
		prevUsedMem[i] = usedMem;

	}

	size_t j = 0;

	int noMemRequired = 0;

	for( size_t i = 0; i < numDomains; i++ )
	{
		size_t takingVcpu = ( i + firstVcupu ) % numDomains;

		if( memRequired[ takingVcpu ] != 0 )
		{
			if( allVmMaxed == 0 )
			{
				for( ; j < numDomains; j++ )
				{
					size_t givingVcpu = ( j + firstVcupu ) % numDomains;
					if( memRequired[ takingVcpu ] != 0 && memSurplus[ givingVcpu ] != 0 )
					{
						long long int memTransfered = MIN( memSurplus[ givingVcpu ], memRequired[ takingVcpu ] );
						virDomainSetMemory( domains[ givingVcpu ], ( ballonSizes[ givingVcpu] - memTransfered ) * 1024 );
						virDomainSetMemory( domains[ takingVcpu ], ( ballonSizes[ takingVcpu ] + memTransfered ) * 1024 );
						memRequired[ takingVcpu ] -= memTransfered;
						memSurplus[ givingVcpu ] -= memTransfered;

						if( memRequired[ takingVcpu ] == 0 )
							break;
					}
				}
			}
			else
			{
				long long int freeMem = virNodeGetFreeMemory( conn )/1024;
				if( freeMem > 200 )
				{
					long long int memTransfered = MIN( 50, MIN( memRequired[ takingVcpu ], freeMem - 200 ) );
					virDomainSetMemory( domains[ takingVcpu ], ( ballonSizes[ takingVcpu ] + memTransfered ) * 1024 );
					memRequired[ takingVcpu ] -= memTransfered;
				}
			}
			noMemRequired = 1;
		}
	}

	if( noMemRequired == 0 )
	{
		for( size_t i = 0; i < numDomains; i++ )
		{
			if( memSurplus[ i ]  > 0 )
				virDomainSetMemory( domains[ i ], ( ballonSizes[ i ] - memSurplus[ i ] ) * 1024 );
		}	
	}

	firstVcupu = ( firstVcupu + 1 ) % numDomains;
	free(info);
	free(memInfo);
	free(ballonSizes);
	free(memRequired);
	free(memSurplus);

}
