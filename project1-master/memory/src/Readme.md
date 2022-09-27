## Memory Coordinator

### APIs used
* **virConnectOpen:** Connect to the hypervisor
* **virConnectClose:** Disconnect tfrom the hypervisor
* **virConnectListAllDomains:** Get the domain pointer and number of domains 
* **virDomainSetMemoryStatsPeriod:** Set the interval to get memory stats 
* **virDomainGetInfo:** Get the virtual CPU stats
* **virDomainMemoryStats:** Get the memory stats fro the virtual CPUs
* **virDomainSetMemory:** Dynamically change the buffer size
* **virDomainFree:** Free the domain pointer

### Flow of the program

* Keep track of the following data from previous iteration <br>
  * Array of previous used memory ( prevUsedMem )<br>
  * First virtual CPU to start coordinating memory from ( CPU offset ) <br><br>

* Iterate over all virtual CPUs<br>
  * Get the ballon size and unused memory<br>
  * Calculate used size ( ballon - unused )<br>
  * If the used memory of a CPU is more than prevUsedMem and unused memory is less than 200
    * Set memory required to max of 50 and ( 200 - unused ) but not more than the CPUs limit<br>
  * If used memory is same or less than prevUsedMem and unused memory is greater than 100
    * Set memory surplus to min of 50 and ( unused - 100 )<br><br>
* Begin memory transfer

### Memory transfer algorithm
* Using a two pointer approach
* I = 0, J = 0
* For I from 0 -> num of virtual CPUs
  * Taking CPU = ( I + CPU offset ) mod numVcpus <br>
  * If Taking CPU requires mempry  <br>
    * For J from J -> num of virtual CPUs
      * Giving CPU = ( J + CPU offset ) mod numVcpus 
      * If Giving CPU has memory surplus
        * Transfer memory from giving CPU to taking CPU
        * Modify Mem required and Mem surplus accordingly
        * If Mem required reaches 0 then break the inner loop <br><br>

* If none of the CPUs can release memory then give all the CPUs that require memory, 50 mb from the host if the host will still be left with more than 200 mbs

* If none of the CPUs need memory then reclaim surplus memory from the CPUs in 50 mb steps. 

* increment CPU offset

