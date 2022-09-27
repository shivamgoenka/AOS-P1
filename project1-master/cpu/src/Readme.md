## VCPU Scheduler

### APIs used
* **virConnectOpen:** Connect to the hypervisor
* **virConnectClose:** Disconnect tfrom the hypervisor
* **virNodeGetCPUMap:** Get the number of pysical CPUs
* **virConnectListAllDomains:** Get the domain pointer and number of domains 
* **virNodeGetCPUStats:** Get the physical CPU utilization stats 
* **virDomainGetInfo:** Get the virtual CPU stats
* **virDomainPinVcpuFlags:** Pin virtual CPUs to Physical CPUs
* **virDomainFree:** Free the domain pointer

### Flow of the program

* Keep track of the following data from previous iteration <br>
  * Array of previous virtual CPU busy time ( prevVcpuTime )<br>
  * Array of previous physical CPU busy time ( prevPcpuTime )<br>
  * Array of previous physical CPU free time ( PrevPcpuFreeTime )<br>
  * Previous standard deviation of phycial CPUs ( prevStd )<br>
  * First physical CPU to start scheduling from ( CPU offset ) <br><br>

* Get the number of physical CPUs (numPcpus) <br>
* Iterate over all Physical CPU<br>
  * Get the busy and free time in nano seconds <br>
  * Get the current interval busy and free time by subtracting prevPcpuTime and PrevPcpuFreeTime <br>
  * Calculate percentage utilization for each physical CPU <br><br>

* Calculate the standard deviation of physical cpu utilization
* if the StDv is greater than 5% and greater or equal to prevStd then reschedule the VCPUs otherwise exit this iteration 

### Scheduling algorithm

* Iterate over all the virtual cpu 
  * Get the cumulative CPU utilization <br>
  * Get the current interval CPU utilization by subtracting prevVcpuTime <br>

* Sort the virtual CPUs on current interval CPU usage by insertion sort
* Starting from CPU offset, pin the virtual CPUs from highest to lowset utilization in a zig-zag fashion 
* increment CPU offset

For example, if there are 4 physical CPU, 8 Virtual CPUs, and CPU offset is 3. The allocation will be: <br>
* Zig
  * V-1 : 100 -> P3
  * V-2 : 90  -> p4
  * V-3 : 80  -> p1
  * V-4 : 70  -> p2
* Zag
  * V-5 : 60  -> p2
  * V-6 : 50  -> p1
  * V-7 : 40  -> p4
  * V-8 : 30  -> p3
