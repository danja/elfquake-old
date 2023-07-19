


./python3 train.py --logdir /log --datadir datasets --config configs/mmnist.yml


.venv/bin/python3 train.py --logdir /log --datadir datasets --config configs/mmnist.yml 
2023-05-29 23:09:42.321732: I tensorflow/tsl/cuda/cudart_stub.cc:28] Could not find cuda drivers on your machine, GPU will not be used.
2023-05-29 23:09:42.368679: I tensorflow/tsl/cuda/cudart_stub.cc:28] Could not find cuda drivers on your machine, GPU will not be used.
2023-05-29 23:09:43.165994: W tensorflow/compiler/tf2tensorrt/utils/py_utils.cc:38] TF-TRT Warning: Could not find TensorRT
Traceback (most recent call last):
  File "/home/danny/elfquake/clockwork/train.py", line 8, in <module>
    from cwvae import build_model
  File "/home/danny/elfquake/clockwork/cwvae.py", line 5, in <module>
    import cnns
  File "/home/danny/elfquake/clockwork/cnns.py", line 5, in <module>
    import tools
  File "/home/danny/elfquake/clockwork/tools.py", line 8, in <module>
    import matplotlib.pyplot as plt
ModuleNotFoundError: No module named 'matplotlib'


./pip install matplotlib

TypeError: load() missing 1 required positional argument: 'Loader'

https://stackoverflow.com/questions/69564817/typeerror-load-missing-1-required-positional-argument-loader-in-google-col

!pip install pyyaml==5.4.1

oops!
.venv/bin/python3 train.py --logdir /log --datadir datasets --config configs/mmnist.yml 

=>

cd elfquake/clockwork

time .venv/bin/python3 train.py --logdir log --datadir datasets --config configs/mmnist.yml 

time .venv/bin/python3 eval.py --logdir log --config configs/mmnist.yml 
