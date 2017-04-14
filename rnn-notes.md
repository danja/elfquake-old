from : http://karpathy.github.io/2015/05/21/rnn-effectiveness/

A more technical explanation is that we use the standard Softmax classifier (also commonly referred to as the cross-entropy loss) on every output vector simultaneously. The RNN is trained with mini-batch Stochastic Gradient Descent and I like to use RMSProp or Adam (per-parameter adaptive learning rate methods) to stablilize the updates.

Temperature. We can also play with the temperature of the Softmax during sampling. Decreasing the temperature from 1 to some lower number (e.g. 0.5) makes the RNN more confident, but also more conservative in its samples. Conversely, higher temperatures will give more diversity but at cost of more mistakes (e.g. spelling mistakes, etc). 


----
from : http://karpathy.github.io/2016/05/31/rl/

It’s interesting to reflect on the nature of recent progress in RL. I broadly like to think about four separate factors that hold back AI:

Compute (the obvious one: Moore’s Law, GPUs, ASICs),
Data (in a nice form, not just out there somewhere on the internet - e.g. ImageNet),
Algorithms (research and ideas, e.g. backprop, CNN, LSTM), and
Infrastructure (software under you - Linux, TCP/IP, Git, ROS, PR2, AWS, AMT, TensorFlow, etc.).
