gpulist=`nvidia-settings -t -q gpus`
gpulist=`echo "$gpulist" | sed -e 's/^ *//'` # no leading spaces
gpulist=`echo "$gpulist" | grep -e '^\['`
 
echo $gpulist | while read LINE; do
        gpuid=`echo "$LINE" | cut -d \  -f 2 | grep -E -o '\[.*\]'`
        gpuname=`echo "$LINE" | cut -d \  -f 3-`
       
        gpuutilstats=`nvidia-settings -t -q "$gpuid"/GPUUtilization | tr ',' '\n'`
        gputemp=`nvidia-settings -t -q "$gpuid"/GPUCoreTemp`
        gputotalmem=`nvidia-settings -t -q "$gpuid"/TotalDedicatedGPUMemory`
        gpuusedmem=`nvidia-settings -t -q "$gpuid"/UsedDedicatedGPUMemory`
 
        gpuusage=`echo "$gpuutilstats"|grep graphics|sed 's/[^0-9]//g'`
        memoryusage=`echo "$gpuutilstats"|grep memory|sed 's/[^0-9]//g'`
        bandwidthusage=`echo "$gpuutilstats"|grep PCIe|sed 's/[^0-9]//g'`
 
        echo "$gpuid $gpuname"
        echo -e "\tRunning at : $gpuusage%"
        echo -e "\tCurrent temperature : $gputemp°C"
        echo -e "\tMemory usage : $gpuusedmem MB/$gputotalmem MB"
        echo -e "\tMemory bandwidth usage : $memoryusage%"
        echo -e "\tPCIe bandwidth usage : $bandwidthusage%"
done

