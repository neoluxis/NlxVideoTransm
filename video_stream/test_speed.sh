#!/bin/bash

# Configuration
SERVER_IP="192.168.123.52"  # neoluxeische
TEST_DURATION=10            # Seconds
PARALLEL_STREAMS=4          # Parallel TCP streams
LOG_FILE="router_speed_test_$(date +%Y%m%d_%H%M%S).log"

# Check if iperf3 is installed
if ! command -v iperf3 &> /dev/null; then
    echo "iperf3 not found. Install it: sudo apt-get install iperf3"
    exit 1
fi

# Function to run iPerf3 server (on neoluxeische)
run_server() {
    echo "Starting iPerf3 server on $SERVER_IP..."
    iperf3 -s -p 5201 >> "$LOG_FILE" 2>&1 &
    SERVER_PID=$!
    echo "Server PID: $SERVER_PID"
    echo "Run client on neolux5 (192.168.123.44)"
    wait $SERVER_PID
}

# Function to run iPerf3 client (on neolux5)
run_client() {
    echo "Starting iPerf3 client, connecting to $SERVER_IP..."
    echo "Test: $TEST_DURATION seconds, $PARALLEL_STREAMS streams"
    echo "Logging to $LOG_FILE"

    iperf3 -c "$SERVER_IP" -p 5201 -t "$TEST_DURATION" -P "$PARALLEL_STREAMS" -f m >> "$LOG_FILE" 2>&1

    echo "Test completed. Summary:"
    grep "sender" "$LOG_FILE" | tail -n 1
    grep "receiver" "$LOG_FILE" | tail -n 1
}

# Check mode
if [[ "$1" == "server" ]]; then
    run_server
elif [[ "$1" == "client" ]]; then
    run_client
else
    echo "Usage: $0 [server|client]"
    echo "On neoluxeische: $0 server"
    echo "On neolux5: $0 client"
    exit 1
fi
