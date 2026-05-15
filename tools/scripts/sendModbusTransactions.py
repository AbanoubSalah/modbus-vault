"""
Name:          sendModbusTransactions.py
Description:   A script to simulate Modbus transactions with microsecond precision.

Timing Note:   This script uses a hybrid busy-wait loop for high-precision timing.
               Accuracy is subject to:
               1. OS Jitter: Best results require real-time priority (SCHED_FIFO).
               2. USB Latency: USB-to-Serial adapters introduce a ~1ms bottleneck.
               3. Modbus Standard: t3.5 is treated as a MINIMUM gap requirement.

Usage Hint:    Run with 'sudo chrt -f 99 python3 ...' on Linux for max precision.
Author:        Abanoub Salah
Version:       1.0.0
Dependencies:  pyserial
License:       MIT
"""

import serial
import time
import random
import argparse

# Pre-Generated Modbus request/response frames
MODBUS_TRANSACTIONS = [
    {
        "request": "010300000001840A",
        "response": "0103020000B844",
        "description": "Read Holding Register: Single register at address 0 (Value: 0)."
    },
    {
        "request": "010300000002C40B",
        "response": "010304112233444BC6",
        "description": "Read Holding Registers: Two registers starting at address 0 (Values: 0x1122, 0x3344)."
    },
    {
        "request": "01040005000121CB",
        "response": "01040201F4B927",
        "description": "Read Input Register: Single register at address 5 (Value: 500)."
    },
    {
        "request": "0106000003E88974",
        "response": "0106000003E88974",
        "description": "Write Single Holding Register: Setting address 0 to value 1000."
    },
    {
        "request": "01100000000204000A0014D3A2",
        "response": "01100000000241C8",
        "description": "Write Multiple Holding Registers: Setting addresses 0-1 to values 10 and 20."
    },
    {
        "request": "0101000000083DCC",
        "response": "010101AAD1F7",
        "description": "Read Coils: 8 coils starting at address 0 (Status: 10101010)."
    },
    {
        "request": "01050000FF008C3A",
        "response": "01050000FF008C3A",
        "description": "Write Single Coil: Setting address 0 to ON (0xFF00)."
    },
    {
        "request": "0105000A0000EDC8",
        "response": "0105000A0000EDC8",
        "description": "Write Single Coil: Setting address 10 to OFF (0x0000)."
    },
    {
        "request": "01020000000879CC",
        "response": "0102013CA199",
        "description": "Read Discrete Inputs: 8 inputs starting at address 0 (Status: 00111100)."
    },
    {
        "request": "010F0000000801553EAA",
        "response": "010F00000008540D",
        "description": "Write Multiple Coils: Setting 8 coils at address 0 (Pattern: 01010101)."
    },
    {
        "request": "010F0000000A0203012408",
        "response": "010F0000000AD5CC",
        "description": "Write Multiple Coils: Setting 10 coils at address 0 (Pattern: 11000000 10)."
    },
    {
        "request": "010301FF0001B5C6",
        "response": "018302C0F1",
        "description": "Read Holding Register: Exception response (Error 0x83) - Illegal Data Address (02)."
    }
]


class FairRandomPicker:
    def __init__(self, arr):
        self.list = arr
        self.tmp_list = []

    def __iter__(self):
        return self

    def __next__(self):
        if not self.tmp_list:
            self.tmp_list = list(self.list)
            random.shuffle(self.tmp_list)
        return self.tmp_list.pop()


def comma_separated_ints(value):
    """Custom type to convert '1,2,3' into [1, 2, 3]."""
    try:
        # Split by comma and convert each part to an integer
        return [int(x.strip()) for x in value.split(",")]
    except ValueError:
        raise argparse.ArgumentTypeError(f"'{value}' is not a valid comma-separated list of integers.")


def micro_sleep(duration_us):
    """
    Precision sleep using a hybrid approach.
    WARNING: USB serial adapters and non-RTOS kernels (like standard Linux)
    will limit the actual physical accuracy to approx 1ms.
    """

    target = time.perf_counter_ns() + int(duration_us * 1_000)
    
    # Sleep for the bulk of the time to save CPU
    # Leaving ~1-2ms for the precision loop
    rem = (target - time.perf_counter_ns()) / 1e9
    if rem > 0.002: 
        time.sleep(rem - 0.001)
        
    # Busy-wait for the final stretch
    while time.perf_counter_ns() < target:
        pass


def send_transaction(ser: serial.Serial, request: bytes, response: bytes, between_delay_us: int, aft_delay_us: int):
    result = True

    try:
        # Send request
        ser.write(request)
        ser.flush()

        # Between transaction delay
        # Small delay to simulate slave processing time
        micro_sleep(between_delay_us)

        # Send response
        ser.write(response)
        ser.flush()

        # After transaction delay
        micro_sleep(aft_delay_us)
    except Exception as e:
        print(f"Sending error: {e}")
        result = False

    return result


def main():
    # Format: 0Xnn 0Xnn ...
    format_frame = lambda frame: (" ".join(["0x"+i+j for i,j in zip(frame[::2], frame[1::2])])) if ((len(frame) % 2) == 0) else ""

    parser = argparse.ArgumentParser(description="Modbus Transaction Generate/Send")
    parser.add_argument("--print-transactions-pool", action="store_true", help="Print transactions pool then exit")
    parser.add_argument("--transactions", type=int, default=float('inf'), help="Number of transactions to send at random")
    parser.add_argument('--transactions-indexes', type=comma_separated_ints, default=list(range(0, len(MODBUS_TRANSACTIONS))), help="Comma-separated list of indexes from transactions pool (e.g., 0,1,2)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate")
    parser.add_argument("--port", type=str, default="/dev/ttyUSB0", help="Serial port")
    parser.add_argument("--include-fail-response", action="store_true", help="Generate wrong responses at random")
    parser.add_argument("--include-fail-timing", action="store_true", help="Generate wrong responses timing at random")
    parser.add_argument("--in-between-res-delay-us", type=float, default=None, help="In between transaction delay in uSeconds. ignored if --include-fail-timing switch present")
    parser.add_argument("--after-res-delay-us", type=float, default=1_000_000, help="After transaction delay in uSeconds")

    args = parser.parse_args()

    if(args.print_transactions_pool):
        # Print transactions pool then exit
        for idx, item in enumerate(MODBUS_TRANSACTIONS):
            request, response, description = item.values()
            print(f'index={idx:}: "{description}" \n\trequest : [{format_frame(request)}]\n\tresponse: [{format_frame(response)}]')
        exit(0)

    try:
        # Connect to serial port
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            parity=serial.PARITY_EVEN,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS,
            timeout=1
            )
        ser.set_low_latency_mode(True) # This reduces the 16ms timer to 1ms
    except Exception as e:
        print(f"Connecting error: {e}")
        exit(1)

    # Bus byte time in uSeconds (standard 11-bits per byte)
    bus_byte_time_us = (11 * 1_000_000 / args.baudrate)
    # Bus 3.5 characters time plus 20%
    bus_t3_5_us = (bus_byte_time_us * 3.5 * 1.2)
    # Between-transaction delay
    default_between_delay_us = bus_t3_5_us if (args.in_between_res_delay_us == None) else args.in_between_res_delay_us

    # Timing failure requested
    timing_fail_choices = [False]
    if args.include_fail_timing:
        if args.transactions > 1:
            # Random pick
            timing_fail_choices.append(True)
        else:
            # Guarantee failure
            timing_fail_choices = [True]

    # Response failure requested
    corrupt_response_choices = [False]
    if args.include_fail_response:
        if args.transactions > 1:
            # Random pick
            corrupt_response_choices.append(True)
        else:
            # Guarantee failure
            corrupt_response_choices = [True]

    try:
        random_frame_generator = FairRandomPicker([MODBUS_TRANSACTIONS[i] for i in args.transactions_indexes])
    except Exception as e:
        print(f"Transactions list error: {e}")
        ser.close()
        exit(1)

    timing_fail_generator = FairRandomPicker(timing_fail_choices)
    corrupt_response_generator = FairRandomPicker(corrupt_response_choices)

    # Send transaction(s)
    transactions_counter = 0
    while transactions_counter < args.transactions:
        transactions_counter += 1

        # Pick a random transaction
        request, response, description = next(random_frame_generator).values()

        # Pick a delay
        timing_fail_flag = next(timing_fail_generator)
        between_delay_us = default_between_delay_us
        if timing_fail_flag == True:
            between_delay_us = (2 * bus_t3_5_us)

        # Check to corrupt response
        corrupt_response_flag = next(corrupt_response_generator)
        if corrupt_response_flag == True:
            response = response[:-2] + f"{int(response[-2:], 16) ^ 1:02X}"

        # Send request
        result = send_transaction(
            ser,
            bytes.fromhex(request),
            bytes.fromhex(response),
            between_delay_us + (bus_t3_5_us * len(request)),
            args.after_res_delay_us
            )

        if result != True:
            break

        print(f'--- Sent requested frame #{transactions_counter} ---')
        if corrupt_response_flag and timing_fail_flag:
            print(f"With corrupt response and wrong timing!")
        elif corrupt_response_flag:
            print(f"With corrupt response!")
        elif timing_fail_flag:
            print(f"With wrong timing response!")

        print(f"Description: {description}\nRequest: {format_frame(request)}\nResponse: {format_frame(response)}")

    ser.close()

if __name__ == "__main__":
    main()
