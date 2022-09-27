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

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE
int numPcpus = 0; // total number of physical cpus 
int firstPcpu = 0; // The CPU to which the first task will be assigned
long long int* prevVcpuTime = NULL;
long long int* currIntVcpuTime = NULL;
long long int* prevPcpuTime = NULL;
long long int* prevPcpuFreeTime = NULL;
double* currIntPcpuUsage = NULL;
int skip = 1;


void CPUScheduler(virConnectPtr conn,int interval);

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

	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);
	numPcpus = virNodeGetCPUMap( conn, NULL, NULL, 0 );

	while(!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		CPUScheduler( conn, interval );
		sleep( interval );
	}

	// Closing the connection
	virConnectClose(conn);
	free( prevVcpuTime );
	free( currIntVcpuTime );
	free( prevPcpuTime );
	free( prevPcpuFreeTime );
	free( currIntPcpuUsage );

	return 0;
}

double stDev( double pcpuTime[], int numPcpu ) {
    double mean = 0.0;
	double SD = 0.0;
    int i;
    for ( i = 0; i < numPcpu; ++i ) {
        mean += pcpuTime[ i ] / numPcpu;
    }
    for ( i = 0; i < numPcpu; ++i)  {
        SD += pow( pcpuTime[ i ] - mean, 2 );
    }
    return sqrt( SD / numPcpu ) ;
}

void swap( virDomainPtr* a, virDomainPtr* b )
{
	virDomainPtr temp = *a;
	*a = *b;
	*b = temp;
}

void swap2( long long int* a, long long int* b )
{
	long long int temp = *a;
	*a = *b;
	*b = temp;
}

void sort( virDomainPtr domains[], long long int currIntVcpuTime[], int numDomains )
{
	size_t maxIdx;
	for( size_t i = 0; i < numDomains - 1; i++ )
	{
		maxIdx = i;
		for( size_t j = i + 1; j < numDomains; j++)
			if( currIntVcpuTime[ j ] > currIntVcpuTime[ maxIdx ] )
				maxIdx = j;
		swap( &domains[ maxIdx ], &domains[ i ] );
		swap2( &currIntVcpuTime[ maxIdx ], &currIntVcpuTime[ i ] );
	}
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
	virDomainPtr *domains;
	virDomainInfoPtr info = malloc( sizeof( virDomainInfo ) );
	size_t numDomains = virConnectListAllDomains( conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING );

	if( prevVcpuTime == NULL )
	{
		prevVcpuTime = calloc( numDomains, sizeof( long long int ) ); // 1 Vcpu per domain
		currIntVcpuTime = calloc( numDomains, sizeof( long long int ) ); // 1 Vcpu per domain
		prevPcpuTime = calloc( numPcpus, sizeof( long long int ) ); 
		prevPcpuFreeTime = calloc( numPcpus, sizeof( long long int ) ); 
		currIntPcpuUsage = calloc( numPcpus, sizeof( double ) ); 
	}
	for( size_t i = 0; i < numPcpus; i++ )
	{
		int nParams = 0;
		long long int currUtilization = 0;
		long long int currFree = 0;

		virNodeGetCPUStats( conn, i, NULL, &nParams, 0 );
		virNodeCPUStatsPtr params = malloc( sizeof( virNodeCPUStats ) * nParams );
		virNodeGetCPUStats( conn, i, params, &nParams, 0 );

		for( size_t j = 0; j < nParams; j++ )
		{
			if( strcmp( params[ j ].field, VIR_NODE_CPU_STATS_IDLE ) == 0 )
				currFree += params[ j ].value / 1000;
			else
				currUtilization += params[ j ].value / 1000;
		}			

		if( prevPcpuTime[ i ] != 0 )
			currIntPcpuUsage[ i ] = ( ( currUtilization - prevPcpuTime[ i ] ) * 100 ) / 
			( ( currUtilization - prevPcpuTime[ i ] ) + ( currFree - prevPcpuFreeTime[ i ] ) );

		prevPcpuTime[ i ] = currUtilization;
		prevPcpuFreeTime[ i ] = currFree;
	}

	for( size_t i = 0; i < numDomains; i++ )
	{
		virDomainGetInfo( domains[ i ], info );

		if( prevVcpuTime[ i ] != 0 )
			currIntVcpuTime[i] = ( info->cpuTime / 1000 ) - prevVcpuTime[ i ];

		prevVcpuTime[ i ] = ( info->cpuTime / 1000 );
	}

	double stDv = stDev( currIntPcpuUsage, numPcpus );
	if( stDv <= 5 || skip == 1 )
	{
		skip = 0;
	}
	else
	{
		sort( domains, currIntVcpuTime, numDomains );

		size_t i = 0;
		int pCpu;
		while( i < numDomains )
		{
			for( size_t j = 0; j < numPcpus && i < numDomains; j++, i++ )
			{
				pCpu = pow( 2, ( j + firstPcpu ) % numPcpus );
				virDomainPinVcpuFlags( domains[ i ], 0, ( unsigned char* ) &pCpu, numPcpus, VIR_DOMAIN_AFFECT_LIVE );
			}

			for( size_t j = numPcpus - 1; j >= 0 && i < numDomains; j--, i++ )
			{
				pCpu = pow( 2, ( j + firstPcpu ) % numPcpus );
				virDomainPinVcpuFlags( domains[ i ], 0, ( unsigned char* ) &pCpu, numPcpus, VIR_DOMAIN_AFFECT_LIVE );
			}
		}
		firstPcpu = ( firstPcpu + 1 ) % numPcpus;
		skip = 1;
	}



	for( size_t i = 0; i < numDomains; i++)
		virDomainFree( domains[ i ] );

	free( info );
}




