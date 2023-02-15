#!/bin/bash

./ramulator hybrid configs/HBM-config.cfg configs/PCM-config.cfg configs/HMA-config.cfg --mode=hma_onfly --stats HMA.stat pin_1M.trc pin_1M.trc > HMA.out 2>&1
