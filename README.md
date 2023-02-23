# Page Migration in Heterogenous Memory Systems

Page migration is one of the techniques to improve memory performance in Hybrid memory systems. More information on this work can be found at https://dl.acm.org/doi/10.1145/3364179

This Hybrid memory model is built on top of Ramulator simulator (https://github.com/CMU-SAFARI/ramulator).

Input Trace format (similar to Ramulator's CPU trace format):
`<num-cpuinst> <memory_address> <IP> <1 for R or 2 for W>`

Getting Started:

  `git clone https://github.com/shashankshashank44/HMA_onfly_adaptive.git`\
  `make -j`\
  `./run.sh`\

This model has been only tested with HBM and PCM memory types.
