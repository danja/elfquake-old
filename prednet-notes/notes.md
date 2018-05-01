
Python3 error
/usr/local/lib/python3.5/dist-packages/hickle.py
line 148
   # if isinstance(f, file):
    #    filename, mode = f.name, f.mode
     #   f.close()
      #  h5f = h5.File(filename, mode)
    if isinstance(f, str) or isinstance(f, unicode): # was elif


Memory probs - trying:

export TF_CUDNN_WORKSPACE_LIMIT_IN_MB=100

kitty_train.py, line 35
batch_size = 2 #was 4

--- very slow, will take days
changed to :

# Training parameters
nb_epoch = 50 # was 150
batch_size = 2 # was 4
samples_per_epoch = 250 # was 500
N_seq_val = 100  # number of sequences to use for validation


danny@lappie:~/prednet$ python3 kitti_train.py
Using TensorFlow backend.
2017-08-19 18:08:54.215243: I tensorflow/stream_executor/cuda/cuda_gpu_executor.cc:901] successful NUMA node read from SysFS had negative value (-1), but there must be at least one NUMA node, so returning NUMA node zero
2017-08-19 18:08:54.215593: I tensorflow/core/common_runtime/gpu/gpu_device.cc:887] Found device 0 with properties: 
name: GeForce 920M
major: 3 minor: 5 memoryClockRate (GHz) 0.954
pciBusID 0000:01:00.0
Total memory: 1.96GiB
Free memory: 1.58GiB
2017-08-19 18:08:54.215610: I tensorflow/core/common_runtime/gpu/gpu_device.cc:908] DMA: 0 
2017-08-19 18:08:54.215615: I tensorflow/core/common_runtime/gpu/gpu_device.cc:918] 0:   Y 
2017-08-19 18:08:54.215621: I tensorflow/core/common_runtime/gpu/gpu_device.cc:977] Creating TensorFlow device (/gpu:0) -> (device: 0, name: GeForce 920M, pci bus id: 0000:01:00.0)
Epoch 1/150

running nvidia-status.sh (pasting)
[gpu:0] (GeForce 920M)
	Running at : 100%
	Current temperature : 71°C
	Memory usage : 1937 MB/2004 MB
	Memory bandwidth usage : 53%
	PCIe bandwidth usage : 67%

see also: nvidia-smi


