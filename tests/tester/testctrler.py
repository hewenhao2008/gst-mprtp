from midboxctrler import MidboxCtrler
from testbedctrler import TestBedCtrler
from flowsctrler import FlowsCtrler
import asyncio

class TestCtrler:
    def __init__(self, testbed_ctrler, midbox_ctrler, flows_ctrler):
        # Construct components
        self.__midbox_ctrler = midbox_ctrler
        self.__flows_ctrler = flows_ctrler
        self.__testbed_ctrler = testbed_ctrler

        # Create connections
        self.__midbox_ctrler.cmd_output.connect(testbed_ctrler.midbox_cmd_input)
        self.__flows_ctrler.sink_cmd_output.connect(testbed_ctrler.sink_cmd_input)
        self.__flows_ctrler.source_cmd_output.connect(testbed_ctrler.source_cmd_input)


    def start(self):
        futures = [self.__midbox_ctrler.start(), self.__flows_ctrler.start()]
        loop = asyncio.get_event_loop()
        loop.run_until_complete(asyncio.wait(futures))
        loop.close()

    def end(self):
        pass
