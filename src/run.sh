#!/bin/bash

# Stream
STREAM_URL="http://5.9.106.210/vlf15"
SECONDS="5"

echo Starting...

# Stream recorder

bin/python3 stream-recorder/icerec.py $STREAM_URL $SECONDS